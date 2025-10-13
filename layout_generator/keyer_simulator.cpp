#include <Python.h>

// Include the core Fingers logic
#include "fingers.cpp"

// Python wrapper functions
static PyObject *score_layout(PyObject *self, PyObject *args) {
  PyObject *key_map_obj;
  const char *text;

  if (!PyArg_ParseTuple(args, "Os", &key_map_obj, &text)) {
    return NULL;
  }

  // Convert Python dict to C++ array (indexed by character code)
  std::vector<Fingers> key_map[256];
  PyObject *key, *value;
  Py_ssize_t pos = 0;

  while (PyDict_Next(key_map_obj, &pos, &key, &value)) {
    // Get character key
    if (!PyUnicode_Check(key)) {
      PyErr_SetString(PyExc_TypeError, "Key must be a string");
      return NULL;
    }

    Py_ssize_t key_size;
    const char *key_str = PyUnicode_AsUTF8AndSize(key, &key_size);
    if (key_size != 1) {
      PyErr_SetString(PyExc_ValueError, "Key must be a single character");
      return NULL;
    }
    unsigned char ch = static_cast<unsigned char>(key_str[0]);

    // Get all chords from list
    if (!PyList_Check(value)) {
      PyErr_SetString(PyExc_TypeError, "Value must be a list");
      return NULL;
    }

    Py_ssize_t num_chords = PyList_Size(value);

    for (Py_ssize_t i = 0; i < num_chords; i++) {
      PyObject *chord_obj = PyList_GetItem(value, i);
      if (!PyUnicode_Check(chord_obj)) {
        PyErr_SetString(PyExc_TypeError, "Chord must be a string");
        return NULL;
      }

      const char *chord_str = PyUnicode_AsUTF8(chord_obj);
      key_map[ch].push_back(Fingers::FromChord(chord_str));
    }
  }

  // Run simulation
  uint64_t cost = type_text(text, key_map);

  return PyLong_FromUnsignedLongLong(cost);
}

// Module methods
static PyMethodDef KeyerMethods[] = {
    {"score_layout", score_layout, METH_VARARGS,
     "Score a keyboard layout by simulating text input"},
    {NULL, NULL, 0, NULL}};

// Module definition
static struct PyModuleDef keyermodule = {
    PyModuleDef_HEAD_INIT, "keyer_simulator_native",
    "Native C++ keyer simulator for performance", -1, KeyerMethods};

// Module initialization
PyMODINIT_FUNC PyInit_keyer_simulator_native(void) {
  return PyModule_Create(&keyermodule);
}
