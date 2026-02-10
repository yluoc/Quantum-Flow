#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define QF_SYMBOL_LEN 16
#define QF_DEFAULT_SOCKET "/tmp/quantumflow_bridge.sock"
#define QF_DEFAULT_QTY_SCALE 100000000ULL

typedef struct {
    char symbol[QF_SYMBOL_LEN];
    uint8_t side;
    uint8_t event_type;
    uint8_t _padding[6];
    double price;
    uint64_t quantity;
    uint64_t timestamp_ns;
    uint64_t order_id;
} MarketDataPacketWire;

typedef struct {
    PyObject_HEAD
    int fd;
    struct sockaddr_un addr;
    char socket_path[sizeof(((struct sockaddr_un*)0)->sun_path)];
    uint64_t sent;
    uint64_t dropped;
} UdsBridgeSenderObject;

static uint64_t to_scaled_qty(double size, uint64_t qty_scale) {
    if (!isfinite(size) || size <= 0.0) {
        return 0;
    }
    long double scaled = (long double)size * (long double)qty_scale;
    if (scaled <= 0.0L) {
        return 0;
    }
    if (scaled >= (long double)UINT64_MAX) {
        return UINT64_MAX;
    }
    return (uint64_t)(scaled + 0.5L);
}

static void copy_symbol(char out[QF_SYMBOL_LEN], const char* symbol, Py_ssize_t len) {
    memset(out, 0, QF_SYMBOL_LEN);
    if (symbol == NULL || len <= 0) {
        return;
    }
    size_t n = (size_t)len;
    if (n > (QF_SYMBOL_LEN - 1)) {
        n = QF_SYMBOL_LEN - 1;
    }
    memcpy(out, symbol, n);
}

static int send_packet(UdsBridgeSenderObject* self, const MarketDataPacketWire* packet) {
    if (self->fd < 0) {
        self->dropped++;
        return -1;
    }

    ssize_t n = sendto(
        self->fd,
        packet,
        sizeof(*packet),
        0,
        (const struct sockaddr*)&self->addr,
        sizeof(self->addr));
    if (n == (ssize_t)sizeof(*packet)) {
        self->sent++;
        return 0;
    }
    self->dropped++;
    return -1;
}

static int level_from_item(PyObject* item, double* price, double* size) {
    PyObject* py_price = PyObject_GetAttrString(item, "price");
    PyObject* py_size = PyObject_GetAttrString(item, "size");
    if (py_price != NULL && py_size != NULL) {
        *price = PyFloat_AsDouble(py_price);
        *size = PyFloat_AsDouble(py_size);
        Py_DECREF(py_price);
        Py_DECREF(py_size);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return 0;
        }
        return 1;
    }
    Py_XDECREF(py_price);
    Py_XDECREF(py_size);
    PyErr_Clear();

    PyObject* seq = PySequence_Fast(item, "level must be a sequence");
    if (seq == NULL) {
        PyErr_Clear();
        return 0;
    }
    if (PySequence_Fast_GET_SIZE(seq) < 2) {
        Py_DECREF(seq);
        return 0;
    }
    PyObject* v0 = PySequence_Fast_GET_ITEM(seq, 0);
    PyObject* v1 = PySequence_Fast_GET_ITEM(seq, 1);
    *price = PyFloat_AsDouble(v0);
    *size = PyFloat_AsDouble(v1);
    Py_DECREF(seq);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        return 0;
    }
    return 1;
}

static int send_levels(
    UdsBridgeSenderObject* self,
    const char* symbol,
    Py_ssize_t symbol_len,
    PyObject* levels,
    uint8_t side,
    uint64_t timestamp_ns,
    uint64_t qty_scale) {
    PyObject* it = PyObject_GetIter(levels);
    if (it == NULL) {
        return -1;
    }

    PyObject* item = NULL;
    while ((item = PyIter_Next(it)) != NULL) {
        double price = 0.0;
        double size = 0.0;
        int ok = level_from_item(item, &price, &size);
        Py_DECREF(item);
        if (!ok) {
            continue;
        }

        MarketDataPacketWire packet;
        memset(&packet, 0, sizeof(packet));
        copy_symbol(packet.symbol, symbol, symbol_len);
        packet.side = side;
        packet.event_type = 0;
        packet.price = price;
        packet.quantity = to_scaled_qty(size, qty_scale);
        packet.timestamp_ns = timestamp_ns;
        (void)send_packet(self, &packet);
    }

    Py_DECREF(it);
    if (PyErr_Occurred()) {
        return -1;
    }
    return 0;
}

static int UdsBridgeSender_init(UdsBridgeSenderObject* self, PyObject* args, PyObject* kwargs) {
    const char* socket_path = QF_DEFAULT_SOCKET;
    static char* kwlist[] = {"socket_path", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s", kwlist, &socket_path)) {
        return -1;
    }

    memset(self->socket_path, 0, sizeof(self->socket_path));
    if (strlen(socket_path) >= sizeof(self->socket_path)) {
        PyErr_SetString(PyExc_ValueError, "Bridge socket path too long");
        return -1;
    }
    snprintf(self->socket_path, sizeof(self->socket_path), "%s", socket_path);
    self->sent = 0;
    self->dropped = 0;

    self->fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (self->fd < 0) {
        PyErr_Format(PyExc_RuntimeError, "Failed to create AF_UNIX socket: %s", strerror(errno));
        return -1;
    }
    int flags = fcntl(self->fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(self->fd, F_SETFL, flags | O_NONBLOCK);
    }

    memset(&self->addr, 0, sizeof(self->addr));
    self->addr.sun_family = AF_UNIX;
    snprintf(self->addr.sun_path, sizeof(self->addr.sun_path), "%s", self->socket_path);
    return 0;
}

static void UdsBridgeSender_dealloc(UdsBridgeSenderObject* self) {
    if (self->fd >= 0) {
        close(self->fd);
        self->fd = -1;
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* UdsBridgeSender_send_trade(UdsBridgeSenderObject* self, PyObject* args, PyObject* kwargs) {
    const char* symbol = NULL;
    Py_ssize_t symbol_len = 0;
    int side = 0;
    double price = 0.0;
    double size = 0.0;
    unsigned long long timestamp_ns = 0;
    unsigned long long order_id = 0;
    unsigned long long qty_scale = QF_DEFAULT_QTY_SCALE;
    static char* kwlist[] = {
        "symbol", "side", "price", "size", "timestamp_ns", "order_id", "qty_scale", NULL};
    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs, "s#iddK|KK", kwlist, &symbol, &symbol_len, &side, &price, &size,
            &timestamp_ns, &order_id, &qty_scale)) {
        return NULL;
    }

    MarketDataPacketWire packet;
    memset(&packet, 0, sizeof(packet));
    copy_symbol(packet.symbol, symbol, symbol_len);
    packet.side = side == 0 ? 0 : 1;
    packet.event_type = 1;
    packet.price = price;
    packet.quantity = to_scaled_qty(size, (uint64_t)qty_scale);
    packet.timestamp_ns = (uint64_t)timestamp_ns;
    packet.order_id = (uint64_t)order_id;

    Py_BEGIN_ALLOW_THREADS
    (void)send_packet(self, &packet);
    Py_END_ALLOW_THREADS

    Py_RETURN_NONE;
}

static PyObject* UdsBridgeSender_send_book(UdsBridgeSenderObject* self, PyObject* args, PyObject* kwargs) {
    const char* symbol = NULL;
    Py_ssize_t symbol_len = 0;
    PyObject* bids = NULL;
    PyObject* asks = NULL;
    unsigned long long timestamp_ns = 0;
    unsigned long long qty_scale = QF_DEFAULT_QTY_SCALE;
    static char* kwlist[] = {"symbol", "bids", "asks", "timestamp_ns", "qty_scale", NULL};
    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs, "s#OOK|K", kwlist, &symbol, &symbol_len, &bids, &asks,
            &timestamp_ns, &qty_scale)) {
        return NULL;
    }

    if (send_levels(self, symbol, symbol_len, bids, 0, (uint64_t)timestamp_ns, (uint64_t)qty_scale) != 0) {
        return NULL;
    }
    if (send_levels(self, symbol, symbol_len, asks, 1, (uint64_t)timestamp_ns, (uint64_t)qty_scale) != 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* UdsBridgeSender_stats(UdsBridgeSenderObject* self, PyObject* Py_UNUSED(ignored)) {
    PyObject* d = PyDict_New();
    if (d == NULL) {
        return NULL;
    }
    PyObject* sent = PyLong_FromUnsignedLongLong(self->sent);
    PyObject* dropped = PyLong_FromUnsignedLongLong(self->dropped);
    PyObject* path = PyUnicode_FromString(self->socket_path);
    PyObject* active = PyBool_FromLong(self->fd >= 0 ? 1 : 0);

    if (sent == NULL || dropped == NULL || path == NULL || active == NULL) {
        Py_XDECREF(sent);
        Py_XDECREF(dropped);
        Py_XDECREF(path);
        Py_XDECREF(active);
        Py_DECREF(d);
        return NULL;
    }

    PyDict_SetItemString(d, "sent", sent);
    PyDict_SetItemString(d, "dropped", dropped);
    PyDict_SetItemString(d, "socket_path", path);
    PyDict_SetItemString(d, "active", active);

    Py_DECREF(sent);
    Py_DECREF(dropped);
    Py_DECREF(path);
    Py_DECREF(active);
    return d;
}

static PyObject* UdsBridgeSender_close(UdsBridgeSenderObject* self, PyObject* Py_UNUSED(ignored)) {
    if (self->fd >= 0) {
        close(self->fd);
        self->fd = -1;
    }
    Py_RETURN_NONE;
}

static PyMethodDef UdsBridgeSender_methods[] = {
    {"send_book", (PyCFunction)UdsBridgeSender_send_book, METH_VARARGS | METH_KEYWORDS, "Send book levels"},
    {"send_trade", (PyCFunction)UdsBridgeSender_send_trade, METH_VARARGS | METH_KEYWORDS, "Send trade packet"},
    {"stats", (PyCFunction)UdsBridgeSender_stats, METH_NOARGS, "Return sender stats"},
    {"close", (PyCFunction)UdsBridgeSender_close, METH_NOARGS, "Close socket"},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject UdsBridgeSenderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "quantumflow_uds_bridge.UdsBridgeSender",
    .tp_basicsize = sizeof(UdsBridgeSenderObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Native UDS sender",
    .tp_methods = UdsBridgeSender_methods,
    .tp_init = (initproc)UdsBridgeSender_init,
    .tp_dealloc = (destructor)UdsBridgeSender_dealloc,
    .tp_new = PyType_GenericNew,
};

static PyMethodDef module_methods[] = {
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    .m_name = "quantumflow_uds_bridge",
    .m_doc = "Native C UDS bridge sender for Python->C++ ingress",
    .m_size = -1,
    .m_methods = module_methods,
};

PyMODINIT_FUNC PyInit_quantumflow_uds_bridge(void) {
    if (PyType_Ready(&UdsBridgeSenderType) < 0) {
        return NULL;
    }

    PyObject* m = PyModule_Create(&module_def);
    if (m == NULL) {
        return NULL;
    }

    Py_INCREF(&UdsBridgeSenderType);
    if (PyModule_AddObject(m, "UdsBridgeSender", (PyObject*)&UdsBridgeSenderType) != 0) {
        Py_DECREF(&UdsBridgeSenderType);
        Py_DECREF(m);
        return NULL;
    }
    return m;
}
