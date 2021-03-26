# desock

modified version of [preeny's desock](https://github.com/zardus/preeny/blob/master/src/desock.c)

- `PREENY_DEBUG=1/0`: show/hide(default) debug level log
- `PREENY_INFO=1/0`: show/hide(default) info level log
- `PREENY_ERROR=1/0`: show(default)/hide error level log
- `PREENY_EXEC_FAST=1`: use poll timeout = -1 && pthread_kill to execute faster
- `PREENY_CLOSE_NOT_EXIT=1`: don't call exit() when close() called
- `PREENY_POLL_TIMEOUT=<int>`: user defined timeout when calling poll()
