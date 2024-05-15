project('doca-router', 'c', 'cpp', default_options: ['buildtype=debug'])

common_dir_path = '/opt/mellanox/doca/applications/common'

add_project_arguments('-DDOCA_ALLOW_EXPERIMENTAL_API', language: 'c')
add_project_arguments('-DDOCA_ALLOW_EXPERIMENTAL_API', language: 'cpp')

source_files = [
	'src/main.cpp',
    'src/dpdk_utils.c',
	'src/core/port.cpp',
]

deps = [
	dependency('doca'),
	dependency('libdpdk'),
]

app_inc_dirs = [
	include_directories(common_dir_path),
	include_directories('src'),
]

executable('doca-sample', source_files, dependencies: deps, include_directories: app_inc_dirs)