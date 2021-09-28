cmake_minimum_required(VERSION 3.16)

project(nbdkit-partclone-filter C)
set(CMAKE_C_STANDARD 99)
set(NBDKIT_PARTCLONE_FILTER nbdkit-partlcone-filter)
set(sources
	partclone-nbd.c
)

add_library(${NBDKIT_PARTCLONE_FILTER} SHARED
	${sources}
)

target_compile_definitions(${NBDKIT_PARTCLONE_FILTER} PUBLIC
	_LARGEFILE64_SOURCE
	_FILE_OFFSET_BITS=64
)

target_include_directories(${NBDKIT_PARTCLONE_FILTER} PRIVATE
	.
)

target_link_libraries(${NBDKIT_PARTCLONE_FILTER} PRIVATE
	pthread
)

install(TARGETS ${NBDKIT_PARTCLONE_FILTER}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
