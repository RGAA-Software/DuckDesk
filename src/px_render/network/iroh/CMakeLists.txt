add_library(net_iroh STATIC iroh_frontend.cpp iroh_server.cpp iroh_session.cpp iroh_transport.cpp)
target_link_libraries(net_iroh PUBLIC net_ws px_transport_cpp)
