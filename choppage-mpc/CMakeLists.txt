# choppage-mpc: the platform-independent MPC app logic, tested on the desktop. The MPC plugin itself is built by
# the MPC Plugin SDK's tools/build.py (see build.sh), which compiles src/*.cpp.
add_library(choppage-mpc-app STATIC app/ChoppageMpcApp.cpp)
target_include_directories(choppage-mpc-app PUBLIC include)
target_link_libraries(choppage-mpc-app PUBLIC choppage-core)

add_executable(test_mpc_app tests/test_mpc_app.cpp)
target_link_libraries(test_mpc_app PRIVATE choppage-mpc-app)
add_test(NAME mpc_app COMMAND test_mpc_app)
