#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <fstream> // Include for file handling

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
double min_power = 95.0;
double max_power = 190.738;

// Fonction pour trier les jobs par leur temps maximum de fin
bool ascending_max_finish_time_job_order(const ::Job* a, const ::Job* b) {
    return a->maximum_finish_time < b->maximum_finish_time;
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
// Function to save energy data to a CSV file with event type and timestamp
void save_energy_to_csv(const std::vector<double>& energy_data, const std::string& filename, const std::string& event_type, double timestamp) {
    std::ofstream file(filename, std::ios_base::app); // Append mode
    if (file.is_open()) {
        file << "Event: " << event_type << ", Timestamp: " << timestamp << "\n";
        for (const auto& energy : energy_data) {
            if (energy != 0.0) {
                file << energy << "\n";
            }
        }
        file.close();
    } else {
        printf("Unable to open file: %s\n", filename.c_str());
    }
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
        std::string event_type;
        switch (event->event_type())
        {
        case fb::Event_BatsimHelloEvent: {
            event_type = "BatsimHelloEvent";
            mb->add_edc_hello("easy", "0.1.0");
        } break;
        case fb::Event_SimulationBeginsEvent: {
            event_type = "SimulationBeginsEvent";
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
            event_type = "JobSubmittedEvent";
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
            event_type = "ProbeDataEmittedEvent";
            auto e = event->event_as_ProbeDataEmittedEvent();

            // Calcul des limites d'augmentation d'énergie par hôte
            double per_host_minimum_energy_increase = (event->timestamp() - last_call_time) * min_power;
            double per_host_maximum_energy_increase = (event->timestamp() - last_call_time) * max_power;

            if (e->probe_id()->str() == "hosts-vec") {
                auto data = e->data_as_VectorialProbeData()->data();
                if (data && data->size() == platform_nb_hosts) {
                    for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
                        double host_energy_diff = data->Get(i) - host_energy[i];

                        // Vérification de la cohérence énergétique par hôte
                        if (last_call_time != -1 && (
                            (host_energy_diff + epsilon < per_host_minimum_energy_increase) ||
                            (host_energy_diff - epsilon > per_host_maximum_energy_increase))) {
                            char err_cstr[512];
                            snprintf(err_cstr, sizeof(err_cstr), 
                                "probe 'hosts-vec' sent an invalid vectorial data: "
                                "host %u's energy increased by %.6f while it should be in the [%.6f, %.6f] range (tested with epsilon=%.6f)",
                                i, host_energy_diff,
                                per_host_minimum_energy_increase, per_host_maximum_energy_increase,
                                epsilon);
                            throw std::runtime_error(err_cstr);
                        }

                        // Mise à jour de l'énergie mesurée pour l'hôte
                        host_energy[i] = data->Get(i);
                    }
                } else {
                    throw std::runtime_error("probe 'hosts-vec' sent an invalid vectorial data: empty or unexpected number of elements");
                }
            } else if (e->probe_id()->str() == "hosts-agg") {
                all_hosts_energy = e->data_as_AggregatedProbeData()->data();
            }

            new_probe_call_time = event->timestamp();

            // Calcul de la somme des énergies par hôte
            double sum_host_energy = 0.0;
            for (const auto& energy : host_energy) {
                sum_host_energy += energy;
            }

            // Vérification de la cohérence énergétique globale
            double diff = std::abs(sum_host_energy - all_hosts_energy);
            if (diff > epsilon) {
                printf("Inconsistent energy data detected: Vectorial sum = %.6f, Aggregated = %.6f, Difference = %.6f\n",
                    sum_host_energy, all_hosts_energy, diff);
            }

            // Sauvegarde des données d'énergie pour ce timestamp
            save_energy_to_csv(host_energy, "energy_data.csv", event_type, parsed->now());
        } break;

        case fb::Event_JobCompletedEvent: {
            event_type = "JobCompletedEvent";
            need_scheduling = true;

            auto job_id = event->event_as_JobCompletedEvent()->job_id()->str();
            auto job_it = running_jobs.find(job_id);
            auto job = job_it->second;
            nb_available_hosts += job->nb_hosts;
            available_hosts += job->alloc;

            delete job;
            running_jobs.erase(job_it);
        } break;
        default: {
            event_type = "UnknownEvent";
        } break;
        }
    }

    // Scheduling logic remains the same
    if (need_scheduling) {
        ::Job* priority_job = nullptr;
        uint32_t nb_available_hosts_at_priority_job_start = 0;
        float priority_job_start_time = -1;

        // First traversal, done until a job cannot be executed right now and is set as the priority job
        // (or until all jobs have been executed)
        auto job_it = job_queue.begin();
        for (; job_it != job_queue.end(); ) {
            auto job = *job_it;
            if (job->nb_hosts <= nb_available_hosts) {
                running_jobs[job->id] = *job_it;
                job->maximum_finish_time = parsed->now() + job->walltime;
                job->alloc = available_hosts.left(job->nb_hosts);
                mb->add_execute_job(job->id, job->alloc.to_string_hyphen());
                available_hosts -= job->alloc;
                nb_available_hosts -= job->nb_hosts;

                job_it = job_queue.erase(job_it);
            }
            else
            {
                priority_job = *job_it;
                ++job_it;

                // compute when the priority job can start, and the number of available machines at this time
                std::vector<::Job*> running_jobs_asc_maximum_finish_time;
                running_jobs_asc_maximum_finish_time.reserve(running_jobs.size());
                for (const auto & it : running_jobs)
                    running_jobs_asc_maximum_finish_time.push_back(it.second);
                std::sort(running_jobs_asc_maximum_finish_time.begin(), running_jobs_asc_maximum_finish_time.end(), ascending_max_finish_time_job_order);

                nb_available_hosts_at_priority_job_start = nb_available_hosts;
                for (const auto & job : running_jobs_asc_maximum_finish_time) {
                    nb_available_hosts_at_priority_job_start += job->nb_hosts;
                    if (nb_available_hosts_at_priority_job_start >= priority_job->nb_hosts) {
                        nb_available_hosts_at_priority_job_start -= priority_job->nb_hosts;
                        priority_job_start_time = job->maximum_finish_time;
                        break;
                    }
                }

                break;
            }
        }

        // Continue traversal to backfill jobs
        for (; job_it != job_queue.end(); ) {
            auto job = *job_it;
            // should the job be backfilled?
            float job_finish_time = parsed->now() + job->walltime;
            if (job->nb_hosts <= nb_available_hosts && // enough resources now?
                (job->nb_hosts <= nb_available_hosts_at_priority_job_start || job_finish_time <= priority_job_start_time)) {  // does not directly hinder the priority job?
                running_jobs[job->id] = *job_it;
                job->maximum_finish_time = job_finish_time;
                job->alloc = available_hosts.left(job->nb_hosts);
                mb->add_execute_job(job->id, job->alloc.to_string_hyphen());
                available_hosts -= job->alloc;
                nb_available_hosts -= job->nb_hosts;

                if (job_finish_time > priority_job_start_time)
                    nb_available_hosts_at_priority_job_start -= job->nb_hosts;

                job_it = job_queue.erase(job_it);
            }
            else if (nb_available_hosts == 0)
                break;
            else
                ++job_it;
        }
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
