#include "tui/log_sink.hpp"

// Explicit template instantiation for the multi-threaded variant
template class trader::RingBufferSinkT<std::mutex>;
