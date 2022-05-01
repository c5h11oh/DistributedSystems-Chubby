#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <optional>

#include "clientlib.cpp"

namespace py = pybind11;

PYBIND11_MODULE(pyclientlib, m) {
  py::class_<SkinnyClient>(m, "SkinnyClient")
      .def(py::init())
      .def("Open", &SkinnyClient::Open, py::arg("path"),
           py::arg("cb") = std::nullopt)
      .def("Close", &SkinnyClient::Close)
      .def("SetContent", &SkinnyClient::SetContent)
      .def("GetContent", &SkinnyClient::GetContent)
      .def("TryAcquire", &SkinnyClient::TryAcquire)
      .def("Acquire", &SkinnyClient::Acquire)
      .def("Release", &SkinnyClient::Release);
}
