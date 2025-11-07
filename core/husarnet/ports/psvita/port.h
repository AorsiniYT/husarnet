#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <netinet/tcp.h>

#include "husarnet/ports/dummy_task_priorities.h"

#define SOCKFUNC(name) ::name
#define SOCKFUNC_close SOCKFUNC(close)

#include "husarnet/ports/psvita/tun.h"