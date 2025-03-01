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
int tache =0;
struct IntervalCounter {
    double start;
    double end;
    uint32_t count;
};

struct PowerDistribution {
    std::vector<IntervalCounter> intervals;

    void initialize(double step, uint32_t num_intervals) {
        intervals.clear();
        for (uint32_t i = 0; i < num_intervals; ++i) {
            intervals.push_back({i * step, (i + 1) * step, 0});
        }
    }

    void update(double value, const std::string &distribution_name) {
        double epsilon = 1e-6;
        bool matched = false;

        printf("Updating power distribution for value: %.6f in %s\n", value, distribution_name.c_str());
        for (auto &interval : intervals) {
            printf("Checking interval [%.2f, %.2f)\n", interval.start, interval.end);
            if (value >= interval.start && value < (interval.end - epsilon)) {
                printf("Matched interval [%.2f, %.2f) in %s\n", interval.start, interval.end, distribution_name.c_str());
                interval.count++;
                matched = true;
                break;
            }
        }

        if (!matched) {
            printf("Value %.6f did not match any interval in %s.\n", value, distribution_name.c_str());
        }
    }

    void print(const std::string &title) const {
        printf("Power Distribution - %s:\n", title.c_str());
        for (const auto &interval : intervals) {
            printf("[%.2f, %.2f): %u\n", interval.start, interval.end, interval.count);
        }
    }
    
    double calculate_average_power() const {
        double total_weighted_power = 0.0;
        uint32_t total_counts = 0;

        for (const auto &interval : intervals) {
            double median_value = (interval.start + interval.end) / 2.0;
            total_weighted_power += median_value * interval.count;
            total_counts += interval.count;
        }

        if (total_counts == 0) {
            return 0.0;
        }

        return total_weighted_power / total_counts;
    }
};
struct Job
{
    std::string id;
    uint32_t nb_hosts;
    double walltime;

    IntervalSet alloc;
    double maximum_finish_time;
    PowerDistribution power_distribution; // Distribution de puissance spécifique à chaque tâche
};

MessageBuilder * mb = nullptr;
bool format_binary = true; // whether flatbuffers binary or json format should be used

uint32_t platform_nb_hosts = 0;
std::list<::Job*> job_queue;
std::unordered_map<std::string, ::Job*> running_jobs;
uint32_t nb_available_hosts = 0;
IntervalSet available_hosts;

bool probes_running = false;

std::vector<PowerDistribution> per_host_distributions;
PowerDistribution task_power_distribution;
std::vector<double> host_energy;
std::vector<double> last_host_energy;
double all_hosts_energy = 0.0;
double last_call_time = -1;
double epsilon = 1e-3;
std::vector<double> host_execution_time;

double min_power = 95.0;
double max_power = 190.738;
double inter_stop_probe_delay = 0.0;
double probe_deadline = 500.0;
std::string behavior = "unset";



void initialize_distributions(double interval_step, uint32_t num_intervals) {
    per_host_distributions.resize(platform_nb_hosts);
    for (auto &distribution : per_host_distributions) {
        distribution.initialize(interval_step, num_intervals);
    }
}

uint8_t batsim_edc_init(const uint8_t *data, uint32_t size, uint32_t flags) {
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags) {
        printf("Unknown flags used, cannot initialize myself.\n");
        return 1;
    }

    mb = new MessageBuilder(!format_binary);

    std::string init_string((const char *)data, static_cast<size_t>(size));
    try {
        auto init_json = json::parse(init_string);
        behavior = init_json["behavior"];
        inter_stop_probe_delay = init_json["inter_stop_probe_delay"];
    } catch (const json::exception &e) {
        throw std::runtime_error("scheduler called with bad init string: " + std::string(e.what()));
    }

    return 0;
}

uint8_t batsim_edc_deinit()
{
    delete mb;
    mb = nullptr;
    for (auto *job : job_queue) {
        delete job;
    }
    job_queue.clear();

    for (auto &entry : running_jobs) {
        delete entry.second;
    }
    running_jobs.clear();
    host_energy.clear();
    last_host_energy.clear();
    per_host_distributions.clear();

    return 0;
}

bool ascending_max_finish_time_job_order(const ::Job* a, const ::Job* b) {
    return a->maximum_finish_time < b->maximum_finish_time;
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
    		last_host_energy.resize(platform_nb_hosts, 0.0);

    		initialize_distributions(100.0, 30);

    		// Initialisez la distribution de puissance pour toutes les tâches
    		task_power_distribution.initialize(100.0, 30);

    		IntervalSet all_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);
    		auto when = TemporalTrigger::make_periodic(1);
    		auto cp = CreateProbe::make_temporal_triggerred(when);
    		cp->set_resources_as_hosts(all_hosts.to_string_hyphen());
    		cp->enable_accumulation_no_reset();
    		mb->add_create_probe("hosts-vec", fb::Metrics_Power, cp);

    		cp->set_resource_aggregation_as_sum();
    		mb->add_create_probe("hosts-agg", fb::Metrics_Power, cp);

    		probes_running = true;

        } break;
        case fb::Event_ProbeDataEmittedEvent: {
                auto e = event->event_as_ProbeDataEmittedEvent();
                static bool has_processed_vec = false, has_processed_agg = false;

                if (e->probe_id()->str() == "hosts-vec") {
                    auto data = e->data_as_VectorialProbeData()->data();
                    for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
                        double host_energy_diff = data->Get(i) - last_host_energy[i];
                        last_host_energy[i] = data->Get(i);
                        host_energy[i] += host_energy_diff;

                        double host_power = host_energy_diff / (event->timestamp() - last_call_time);
                        per_host_distributions[i].update(host_power, "Host " + std::to_string(i) + " Power Distribution");
                    }
                    has_processed_vec = true;
                }

                if (e->probe_id()->str() == "hosts-agg") {
                    double prev_all_hosts_energy = all_hosts_energy;
                    all_hosts_energy = e->data_as_AggregatedProbeData()->data();

                    double total_power = (all_hosts_energy - prev_all_hosts_energy) / (event->timestamp() - last_call_time);
                    task_power_distribution.update(total_power, "Task Power Distribution");

                    for (auto &[job_id, job] : running_jobs) {
                        double job_power = total_power / running_jobs.size();
                        job->power_distribution.update(job_power, "Task " + job_id);
                    }

                    has_processed_agg = true;
                }

                if (has_processed_vec && has_processed_agg) {
                    last_call_time = event->timestamp();
                    has_processed_vec = has_processed_agg = false;
                }
               
               std::unordered_map<uint32_t, double> host_power_sum;
               
    //  Parcours des jobs en cours pour remplir les sommes
    for (const auto& [job_id, job] : running_jobs) {
        if (job->alloc.is_empty()) {
            printf("Warning: job %s has an empty allocation.\n", job_id.c_str());
            continue;
        }

        //  Parcours des hôtes utilisés par ce job
        for (auto it = job->alloc.intervals_begin(); it != job->alloc.intervals_end(); ++it) {
            int lower = it->lower();
            int upper = it->upper();

            if (lower < 0 || upper >= platform_nb_hosts) {
                printf("Error: job %s tries to access invalid host range [%d, %d]\n", job_id.c_str(), lower, upper);
                continue;
            }

            for (int host = lower; host <= upper; ++host) {
                if (host_energy.size() <= (size_t)host) {
                    printf("Error: Host %d out of bounds in energy/time vectors\n", host);
                    continue;
                }

                // 📊 Ajout des valeurs pour cet hôte
                host_power_sum[host] += host_energy[host];
            }
              
        }
    }

    // 📢 Affichage des résultats
    //printf("\n=== Résumé de la consommation des hôtes actifs ===\n");
    //for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
       // if (host_power_sum.find(i) != host_power_sum.end() ) {
        //    double avg_power =  (host_power_sum[i] / last_call_time);
         //   printf("Host %u - Total Power: %.2f, Total Time: %.2f, Avg Power: %.2f W\n", 
          //         i, host_power_sum[i],last_call_time , avg_power);
       // }
   // }
            } break;
        case fb::Event_JobSubmittedEvent: {
            ::Job job{
                event->event_as_JobSubmittedEvent()->job_id()->str(),
                event->event_as_JobSubmittedEvent()->job()->resource_request(),
                event->event_as_JobSubmittedEvent()->job()->walltime(),
                IntervalSet::empty_interval_set(),
                -1
            };
            job.power_distribution.initialize(10.0, 20); // Initialisation de la distribution de puissance par tâche
                printf("[JOB SUBMIT] Job %s soumis avec %u hosts et %.2f walltime\n",
                    job.id.c_str(), job.nb_hosts, job.walltime);

            if (job.nb_hosts > platform_nb_hosts)
                mb->add_reject_job(job.id);
            else if (job.walltime <= 0)
                mb->add_reject_job(job.id);
            else {
                need_scheduling = true;
                job_queue.emplace_back(new ::Job(job));
            }
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

    if (need_scheduling) {

        std::vector<std::pair<uint32_t, double>> sorted_hosts;
        for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
            double avg_power = per_host_distributions[i].calculate_average_power();
            sorted_hosts.emplace_back(i, avg_power);
        }
        std::sort(sorted_hosts.begin(), sorted_hosts.end(),
          [](const std::pair<uint32_t, double>& a, const std::pair<uint32_t, double>& b) {
              return a.second < b.second;
          });

    	// Calculer la puissance moyenne pour toutes les tâches
    	double average_task_power = task_power_distribution.calculate_average_power();
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
                //job->alloc = available_hosts.left(job->nb_hosts);
                IntervalSet selected_hosts;
                uint32_t allocated_count = 0;

                for (const auto& [host, _] : sorted_hosts) {
                    if (allocated_count >= job->nb_hosts) break;  
                    if (available_hosts.contains(host)) {  
                        selected_hosts.insert(host);
                        allocated_count++;
                    }
                }
                job->alloc = selected_hosts;
                tache=tache+1; 
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
                IntervalSet selected_hosts;
                uint32_t allocated_count = 0;

                for (const auto& [host, _] : sorted_hosts) {
                    if (allocated_count >= job->nb_hosts) break;  
                    if (available_hosts.contains(host)) {  
                        selected_hosts.insert(host);
                        allocated_count++;
                    }
                }
                job->alloc = selected_hosts;
                //job->alloc = available_hosts.left(job->nb_hosts);
                mb->add_execute_job(job->id, job->alloc.to_string_hyphen());
                tache=tache+1; 
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
    //tache=tache+1; 
    //if (probes_running && job_queue.empty() && running_jobs.empty()) 
    if (probes_running && tache >=200){
        //printf("[ERROR] Arrêt prématuré des probes alors que des jobs devraient être exécutés !\n");
        mb->add_stop_probe("hosts-vec");
        mb->add_stop_probe("hosts-agg");
        probes_running = false;
    }

    mb->finish_message(parsed->now());
    serialize_message(*mb, !format_binary, const_cast<const uint8_t **>(decisions), decisions_size);
    return 0;
}
