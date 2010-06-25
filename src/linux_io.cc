
#include <unistd.h>
#include <stdio.h>
#include "arch/io.hpp"
#include "utils.hpp"
#include "event_queue.hpp"
#include "alloc/memalign.hpp"
#include "alloc/pool.hpp"
#include "alloc/dynamic_pool.hpp"
#include "alloc/stats.hpp"
#include "alloc/alloc_mixin.hpp"

ssize_t io_calls_t::read(resource_t fd, void *buf, size_t count) {
    return ::read(fd, buf, count);
}

ssize_t io_calls_t::write(resource_t fd, const void *buf, size_t count) {
    return ::write(fd, buf, count);
}

void io_calls_t::schedule_aio_read(resource_t resource,
                                   size_t offset, size_t length, void *buf,
                                   event_queue_t *notify_target, iocallback_t *callback)
{
    iocb *request = new iocb();
    io_prep_pread(request, resource, buf, length, offset);
    io_set_eventfd(request, notify_target->aio_notify_fd);
    request->data = callback;
    iocb* requests[1];
    requests[0] = request;
    int res = io_submit(notify_target->aio_context, 1, requests);
    check("Could not submit IO read request", res < 1);
}

// TODO: we should build a flow control/diagnostics system into the
// AIO scheduler. If the system can't handle too much IO, we should
// slow down responses to the client in case they're using our API
// synchroniously, and also stop reading from sockets so their socket
// buffers fill up during sends in case they're using our API
// asynchronously.
void io_calls_t::schedule_aio_write(aio_write_t *writes, int num_writes, event_queue_t *notify_target) {
    // TODO: watch how we're allocating
    iocb* requests[num_writes];
    int i;
    for (i = 0; i < num_writes; i++) {
        iocb *request = new iocb();
        io_prep_pwrite(request, writes[i].resource, writes[i].buf, writes[i].length, writes[i].offset);
        io_set_eventfd(request, notify_target->aio_notify_fd);
        request->data = writes[i].callback;
        requests[i] = request;
    }

    int num_submitted = 0;
    while(num_submitted != num_writes) {
        int res = io_submit(notify_target->aio_context, num_writes - num_submitted, requests + num_submitted);
#ifndef NDEBUG
        if(res < 1) {
            printf("Sumitted %d requests so far, %d left\n", num_submitted, num_writes - num_submitted);
        }
#endif
        check("Could not submit IO write request", res < 1);
        num_submitted += res;
    }
}

void io_calls_t::aio_notify(event_t *event) {
    // Notify the interested party about the event
    iocallback_t *callback = (iocallback_t*)event->state;
    event->state = NULL;
    callback->on_io_complete(event);
}
