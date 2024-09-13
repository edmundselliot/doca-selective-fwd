project('doca-router', ['c','cpp'], default_options: ['buildtype=debug'])

add_project_arguments('-DDOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])

source_files = [
	'src/main.cpp',
	'src/pipes.cpp',
	'src/worker_pmd.cpp',
    'src/worker_offload.cpp',
	'src/flow_common.cpp',
    'src/dpdk_utils.c',
]

deps = [
	dependency('doca-flow'),
	dependency('doca-argp'),
	dependency('doca-common'),
	dependency('libdpdk'),
]

app_inc_dirs = [
	include_directories('src'),
]

executable(
	'doca-selective-fwd',
	source_files,
	dependencies: deps,
	include_directories: app_inc_dirs
)
