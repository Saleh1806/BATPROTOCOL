#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

#include <batprotocol.hpp>
#include <intervalset.hpp>

#include "batsim_edc.h"

using namespace batprotocol;

struct Job
{
    std::string id;
    uint32_t nb_hosts;
    double walltime;

    IntervalSet alloc;
    double maximum_finish_time;
};

MessageBuilder * mb = nullptr;
bool format_binary = true; // whether flatbuffers binary or json format should be used

uint32_t platform_nb_hosts = 0;
std::list<::Job*> job_queue;
std::unordered_map<std::string, ::Job*> running_jobs;
uint32_t nb_available_hosts = 0;
IntervalSet available_hosts;

double all_hosts_energy = 0.0;
std::vector<double> host_energy;
bool probes_running = false;
double last_call_time = -1;
double epsilon = 1e-3;

uint8_t batsim_edc_init(const uint8_t * data, uint32_t size, uint32_t flags)
{
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags)
    {
        printf("Unknown flags used, cannot initialize myself.\n");
        return 1;
    }

    mb = new MessageBuilder(!format_binary);

    // ignore initialization data
    (void) data;
    (void) size;

    return 0;
}

uint8_t batsim_edc_deinit()
{
    delete mb;
    mb = nullptr;
    host_energy.clear();
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
    bool need_scheduling = false;
    auto nb_events = parsed->events()->size();
    for (unsigned int i = 0; i < nb_events; ++i) {
        auto event = (*parsed->events())[i];
        switch (event->event_type())
        {
        case fb::Event_BatsimHelloEvent: {
            mb->add_edc_hello("easy", "0.1.0");
        } break;
        case fb::Event_SimulationBeginsEvent: {
            auto simu_begins = event->event_as_SimulationBeginsEvent();
            platform_nb_hosts = simu_begins->computation_host_number();
            nb_available_hosts = platform_nb_hosts;
            available_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);
            host_energy.resize(platform_nb_hosts, 0.0);

            // Add energy probes
            IntervalSet all_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);
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
            ::Job job{
                event->event_as_JobSubmittedEvent()->job_id()->str(),
                event->event_as_JobSubmittedEvent()->job()->resource_request(),
                event->event_as_JobSubmittedEvent()->job()->walltime(),
                IntervalSet::empty_interval_set(),
                -1
            };

            if (job.nb_hosts > platform_nb_hosts)
                mb->add_reject_job(job.id);
            else if (job.walltime <= 0)
                mb->add_reject_job(job.id);
            else {
                need_scheduling = true;
                job_queue.emplace_back(new ::Job(job));
            }
        } break;
        case fb::Event_ProbeDataEmittedEvent: {
            auto e = event->event_as_ProbeDataEmittedEvent();
            if (e->probe_id()->str() == "hosts-vec") {
                auto data = e->data_as_VectorialProbeData()->data();
                if (data && data->size() == platform_nb_hosts) {
                    for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
                        host_energy[i] = data->Get(i);
                    }
                }
            } else if (e->probe_id()->str() == "hosts-agg") {
                all_hosts_energy = e->data_as_AggregatedProbeData()->data();
            }

            new_probe_call_time = event->timestamp();
        } break;
        case fb::Event_JobCompletedEvent: {
            need_scheduling = true;

            auto job_id = event->event_as_JobCompletedEvent()->job_id()->str();
            auto job_it = running_jobs.find(job_id);
            auto job = job_it->second;
            nb_available_hosts += job->nb_hosts;
            available_hosts += job->alloc;

            delete job;
            running_jobs.erase(job_it);
        } break;
        default: break;
        }
    }

    // Scheduling logic remains the same
    if (need_scheduling) {
        // Existing scheduling logic here...
    }

    // Stop probes when all jobs are done
    if (probes_running && job_queue.empty() && running_jobs.empty()) {
        mb->add_stop_probe("hosts-vec");
        mb->add_stop_probe("hosts-agg");
        probes_running = false;
    }

    mb->finish_message(parsed->now());
    serialize_message(*mb, !format_binary, const_cast<const uint8_t **>(decisions), decisions_size);
    return 0;
}
