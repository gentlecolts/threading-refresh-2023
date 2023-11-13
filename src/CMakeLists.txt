find_package(OpenMP REQUIRED)

add_executable(${PROJECT_NAME}
	main.cpp
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)

