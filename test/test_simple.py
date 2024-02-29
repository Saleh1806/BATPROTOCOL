#!/usr/bin/env python3
'''Simple tests.

These tests run batsim with most basic features (execute jobs, reject jobs).
'''
import inspect
import json
import os
import subprocess
import pytest
import pandas as pd

from helper import prepare_instance, run_batsim

MOD_NAME = __name__.replace('test_', '', 1)

@pytest.fixture(scope="module", params=[False, True])
def use_json(request):
    return request.param

def test_rejecter(test_root_dir):
    platform = 'small_platform'
    workload = 'test_delays'
    func_name = inspect.currentframe().f_code.co_name.replace('test_', '', 1)
    instance_name = f'{MOD_NAME}-{func_name}'

    batcmd, outdir, _ = prepare_instance(instance_name, test_root_dir, platform, 'rejecter', workload)
    p = run_batsim(batcmd, outdir)
    assert p.returncode == 0

def test_1by1(test_root_dir):
    platform = 'small_platform'
    workload = 'test_delays'
    func_name = inspect.currentframe().f_code.co_name.replace('test_', '', 1)
    instance_name = f'{MOD_NAME}-{func_name}'

    batcmd, outdir, _ = prepare_instance(instance_name, test_root_dir, platform, 'exec1by1', workload)
    p = run_batsim(batcmd, outdir)
    assert p.returncode == 0

def test_fcfs(test_root_dir, use_json):
    platform = 'small_platform'
    workload = 'test_delays'
    func_name = inspect.currentframe().f_code.co_name.replace('test_', '', 1)
    instance_name = f'{MOD_NAME}-{func_name}-' + str(int(use_json))

    batcmd, outdir, _ = prepare_instance(instance_name, test_root_dir, platform, 'fcfs', workload, use_json=use_json)
    p = run_batsim(batcmd, outdir)
    assert p.returncode == 0

def test_easy(test_root_dir, use_json):
    platform = 'cluster512'
    workload = 'example_workload_hpc_seed3_jobs250'
    func_name = inspect.currentframe().f_code.co_name.replace('test_', '', 1)
    instance_name = f'{MOD_NAME}-{func_name}-' + str(int(use_json))

    batcmd, outdir, _ = prepare_instance(instance_name, test_root_dir, platform, 'easy', workload, use_json=use_json, batsim_extra_args=['--mmax-workload'])
    p = run_batsim(batcmd, outdir)
    assert p.returncode == 0

def compute_expected_alloc(row):
    return str(row['allocated_resources']) == str(row['expected_allocation'])

def test_extra_data(test_root_dir, use_json):
    platform = 'small_platform'
    workload = 'test_extra_data_desired_allocation'
    func_name = inspect.currentframe().f_code.co_name.replace('test_', '', 1)
    instance_name = f'{MOD_NAME}-{func_name}-' + str(int(use_json))

    batcmd, outdir, workload_file = prepare_instance(instance_name, test_root_dir, platform, 'static', workload, use_json=use_json, batsim_extra_args=['--mmax-workload'])
    p = run_batsim(batcmd, outdir)
    assert p.returncode == 0

    batjobs_filename = f'{outdir}/batout/jobs.csv'
    jobs = pd.read_csv(batjobs_filename)

    f = open(workload_file)
    batw = json.load(f)
    job_ids = list()
    expected_allocations = list()

    for job in batw['jobs']:
        extra_data = json.loads(job['extra_data'])
        job_ids.append(job['id'])
        expected_allocations.append(extra_data['desired_allocation'])

    expected_df = pd.DataFrame({'job_id': job_ids, 'expected_allocation': expected_allocations})
    df = jobs.merge(expected_df, on='job_id', how='inner')

    df['job_expected_alloc'] = df.apply(compute_expected_alloc, axis=1)

    with pd.option_context('display.max_rows', None, 'display.max_columns', None):
        if not df['job_expected_alloc'].all():
            unexpected_df = df[df['job_expected_alloc'] == False]
            printable_df = unexpected_df[['job_id', 'job_expected_alloc', 'expected_allocation', 'allocated_resources']]
            print('Some jobs have an unexpected allocation')
            print(printable_df)
            raise ValueError('Some jobs have an unexpected allocation')
        else:
            print('All jobs are valid!')
            printable_df = df[['job_id', 'expected_allocation', 'allocated_resources']]
            print(printable_df)
