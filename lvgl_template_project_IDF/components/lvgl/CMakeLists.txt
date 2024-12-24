file(GLOB_RECURSE SOURCES src/*.c)
file(GLOB_RECURSE DEMOS_SOURCES demos/*.c)
file(GLOB_RECURSE EXAMPLES_SOURCES examples/*.c)
idf_component_register(	SRCS ${SOURCES}
						SRCS ${DEMOS_SOURCES}
						SRCS ${EXAMPLES_SOURCES}
						INCLUDE_DIRS . src ../ demos examples) 