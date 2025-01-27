#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>

#include <batprotocol.hpp>
#include <intervalset.hpp>

#include <nlohmann/json.hpp>

#include "batsim_edc.h"

using namespace batprotocol;
using json = nlohmann::json;

struct SchedJob
{
    std::string job_id;
    uint8_t nb_hosts;
};

struct IntervalCounter {
    double start;
    double end;
    uint32_t count;
};

struct PowerDistribution {
    std::vector<IntervalCounter> intervals;

    // Initialize intervals with a specified step and range
    void initialize(double step, uint32_t num_intervals) {
        intervals.clear();
        for (uint32_t i = 0; i < num_intervals; ++i) {
            intervals.push_back({i * step, (i + 1) * step, 0});
        }
    }

    // Increment the count of the interval where the value falls
    void update(double value) {
        for (auto &interval : intervals) {
            if (value >= interval.start && value < interval.end) {
                interval.count++;
                return;
            }
        }
    }

    // Print the intervals and their counts
    void print() const {
        printf("Power Distribution:\n");
        for (const auto &interval : intervals) {
            printf("[%.2f, %.2f): %u\n", interval.start, interval.end, interval.count);
        }
    }
};

MessageBuilder * mb = nullptr;
bool format_binary = true; // whether flatbuffers binary or json format should be used
std::list<SchedJob*> * jobs = nullptr;
SchedJob * currently_running_job = nullptr;
uint32_t platform_nb_hosts = 0;
bool probes_running = false;
bool all_jobs_submitted = false;
double min_power = 95.0;
double max_power = 190.738;
double last_call_time = -1;
double epsilon = 1e-3;
double inter_stop_probe_delay = 0.0;
double probe_deadline = 500.0;
std::string behavior = "unset";

double all_hosts_energy = 0.0;
std::vector<double> host_energy;
std::vector<double> last_host_energy;

PowerDistribution host_power_distribution;
PowerDistribution task_power_distribution;

void initialize_distributions(double interval_step, uint32_t num_intervals) {
    host_power_distribution.initialize(interval_step, num_intervals);
    task_power_distribution.initialize(interval_step, num_intervals);
}

void update_power_distribution(const std::vector<double> &energy_data, double elapsed_time, PowerDistribution &distribution) {
    for (const auto &energy : energy_data) {
        double power = energy / elapsed_time;
        distribution.update(power);
    }
}

uint8_t batsim_edc_init(const uint8_t * data, uint32_t size, uint32_t flags)
{
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags)
    {
        printf("Unknown flags used, cannot initialize myself.\n");
        return 1;
    }

    mb = new MessageBuilder(!format_binary);
    jobs = new std::list<SchedJob*>();

    std::string init_string((const char *)data, static_cast<size_t>(size));
    try {
        auto init_json = json::parse(init_string);
        behavior = init_json["behavior"];
        inter_stop_probe_delay = init_json["inter_stop_probe_delay"];
    } catch (const json::exception & e) {
        throw std::runtime_error("scheduler called with bad init string: " + std::string(e.what()));
    }

    initialize_distributions(10.0, 30); // Example: 30 intervals of 10 units each

    return 0;
}

uint8_t batsim_edc_deinit()
{
    delete mb;
    mb = nullptr;

    host_energy.clear();

    if (jobs != nullptr)
    {
        for (auto * job : *jobs)
        {
            delete job;
        }
        delete jobs;
        jobs = nullptr;
    }

    return 0;
}

uint8_t batsim_edc_take_decisions(
    const uint8_t * what_happened,
    uint32_t what_happened_size,
    uint8_t ** decisions,
    uint32_t * decisions_size)
{
    (void) what_happened_size;
    auto * parsed = deserialize_message(*mb, !format_binary, what_happened);
    mb->clear(parsed->now());

    double new_probe_call_time = -1;

    auto nb_events = parsed->events()->size();
    for (unsigned int i = 0; i < nb_events; ++i) {
        auto event = (*parsed->events())[i];
        switch (event->event_type())
        {
        case fb::Event_BatsimHelloEvent: {
            mb->add_edc_hello("probe-energy", "0.1.0");
        } break;
        case fb::Event_SimulationBeginsEvent: {
            auto simu_begins = event->event_as_SimulationBeginsEvent();
            platform_nb_hosts = simu_begins->computation_host_number();
            host_energy.resize(platform_nb_hosts, 0.0);
            last_host_energy.resize(platform_nb_hosts, 0.0);

            IntervalSet all_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts-1);
            auto when = TemporalTrigger::make_periodic(1);
            auto cp = batprotocol::CreateProbe::make_temporal_triggerred(when);
            cp->set_resources_as_hosts(all_hosts.to_string_hyphen());
            cp->enable_accumulation_no_reset();
            mb->add_create_probe("hosts-vec", batprotocol::fb::Metrics_Power, cp);

            cp->set_resource_aggregation_as_sum();
            mb->add_create_probe("hosts-agg", batprotocol::fb::Metrics_Power, cp);

            probes_running = true;
        } break;
        case fb::Event_JobSubmittedEvent: {
            auto parsed_job = event->event_as_JobSubmittedEvent();
            auto job = new SchedJob();
            job->job_id = parsed_job->job_id()->str();

            job->nb_hosts = parsed_job->job()->resource_request();
            if (job->nb_hosts > platform_nb_hosts) {
                mb->add_reject_job(job->job_id);
                delete job;
            } else {
                jobs->push_back(job);
            }
        } break;
        case fb::Event_JobCompletedEvent: {
            delete currently_running_job;
            currently_running_job = nullptr;
        } break;
        case fb::Event_AllStaticJobsHaveBeenSubmittedEvent: {
            all_jobs_submitted = true;
        } break;
        case fb::Event_ProbeDataEmittedEvent: {
            auto e = event->event_as_ProbeDataEmittedEvent();
            if (last_call_time == -1) {
                last_call_time = event->timestamp();
                continue;
            }

            double elapsed_time = event->timestamp() - last_call_time;

            if (e->probe_id()->str() == "hosts-vec") {
                auto data = e->data_as_VectorialProbeData()->data();
                if (data == nullptr || data->size() != platform_nb_hosts) {
                    throw std::runtime_error("probe 'hosts-vec' sent an invalid vectorial data: empty or unexpected number of elements");
                }

                for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
                    double energy_diff = data->Get(i) - last_host_energy[i];
                    last_host_energy[i] = data->Get(i);
                    host_energy[i] += energy_diff;
                }

                update_power_distribution(host_energy, elapsed_time, host_power_distribution);
            } else if (e->probe_id()->str() == "hosts-agg") {
                all_hosts_energy = e->data_as_AggregatedProbeData()->data();
                double power = all_hosts_energy / elapsed_time;
                task_power_distribution.update(power);
            }

            new_probe_call_time = event->timestamp();
        } break;
        default: break;
        }
    }

    // if probe data has been received, update time & check energy consistency
    if (new_probe_call_time != -1) {
        if (last_call_time != -1) {
            double my_sum_energy = 0.0;
            for (auto & e : host_energy)
                my_sum_energy += e;

            double total_energy_diff = all_hosts_energy - my_sum_energy;
            if (fabs(total_energy_diff) > epsilon) {
                char * err_cstr;
                asprintf(&err_cstr, "inconsistent energy state: the aggregated probe last value is %.6f, while the sum the vectorial probe last value is %.6f (tested with epsilon=%.6f)",
                    all_hosts_energy, my_sum_energy,
                    epsilon
                );
                std::string err(err_cstr);
                free(err_cstr);
                throw std::runtime_error(err);
            }
        }
        last_call_time = new_probe_call_time;
    }

    // execute jobs one by one
    if (currently_running_job == nullptr && !jobs->empty()) {
        currently_running_job = jobs->front();
        jobs->pop_front();
        auto hosts = IntervalSet(IntervalSet::ClosedInterval(0, currently_running_job->nb_hosts-1));
        mb->add_execute_job(currently_running_job->job_id, hosts.to_string_hyphen());
    }

    // stop probes when all jobs have been executed, so the simulation can finish
    double msg_date = parsed->now();
    bool stop_probes = false;
    if (behavior == "wload") {
        stop_probes = probes_running && all_jobs_submitted && currently_running_job == nullptr && jobs->size() == 0;
    } else if (behavior == "deadline") {
        stop_probes = probes_running && msg_date >= probe_deadline;
    }

    if (stop_probes) {
        printf("probe-energy stopping probes\n");
        mb->add_stop_probe("hosts-vec");
        msg_date += inter_stop_probe_delay;
        mb->set_current_time(msg_date);
        mb->add_stop_probe("hosts-agg");
        probes_running = false;
    }

    mb->finish_message(msg_date);
    serialize_message(*mb, !format_binary, const_cast<const uint8_t **>(decisions), decisions_size);

    // Print the power distribution tables for debugging
    host_power_distribution.print();
    task_power_distribution.print();

    return 0;
}
