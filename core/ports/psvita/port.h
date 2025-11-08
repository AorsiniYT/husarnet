#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <netinet/tcp.h>

#define SOCKFUNC(name) ::name
#define SOCKFUNC_close SOCKFUNC(close)

#include "husarnet/ports/psvita/tun.h"