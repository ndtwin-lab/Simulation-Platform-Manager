.PHONY: all simulator request_manager server clean_running clean_exec clean

CXX = g++
CXXFLAGS = -std=c++17 -Iinclude -Wall
SPDLOGFLAGS = -lspdlog -lfmt
BOOSTFLAGS = -lpthread -lboost_system -lboost_thread
BOOSTFLAGS_SERVER = -lpthread -lboost_system -lboost_thread -lboost_filesystem
LOGGER = Logger.cpp

all: simulator request_manager server app

simulator: $(LOGGER) registered/simple_sim/1.0/simple_sim.cpp
	$(CXX) $(CXXFLAGS) $(LOGGER) registered/simple_sim/1.0/simple_sim.cpp -o registered/simple_sim/1.0/executable $(SPDLOGFLAGS)

request_manager: $(LOGGER) request_manager.cpp include/settings/request_manager.hpp
	$(CXX) $(CXXFLAGS) $(LOGGER) request_manager.cpp -o request_manager $(BOOSTFLAGS) $(SPDLOGFLAGS)

server: $(LOGGER) simulation_server.cpp include/settings/sim_server.hpp include/types/sim_server.hpp
	$(CXX) $(CXXFLAGS) $(LOGGER) simulation_server.cpp -o simulation_server $(BOOSTFLAGS_SERVER) $(SPDLOGFLAGS)

app: $(LOGGER) app.cpp include/settings/app.hpp include/types/app.hpp
	$(CXX) $(CXXFLAGS) $(LOGGER) app.cpp -o app $(BOOSTFLAGS) $(SPDLOGFLAGS)

clean_running:
	rm -rf /srv/nfs/sim/*/*

clean_exec:
	rm -f registered/*/*/executable
	rm -f request_manager
	rm -f sim_server server
	rm -f app

clean: clean_running clean_exec
