project('doca-router', 'c', default_options: ['buildtype=debug'])

add_project_arguments('-DDOCA_ALLOW_EXPERIMENTAL_API', language: 'c')

source_files = [
	'src/main.c',
	'src/pipes.c',
	'src/flow_common.c',
    'src/dpdk_utils.c',
]

deps = [
	dependency('doca'),
	dependency('libdpdk'),
]

app_inc_dirs = [
	include_directories('src'),
]

executable('doca-sample', source_files, dependencies: deps, include_directories: app_inc_dirs)