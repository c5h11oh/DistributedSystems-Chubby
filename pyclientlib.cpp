#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <optional>

#include "clientlib.cpp"

namespace py = pybind11;

PYBIND11_MODULE(pyclientlib, m) {
  py::class_<SkinnyClient>(m, "SkinnyClient")
      .def(py::init(), py::call_guard<py::gil_scoped_release>())
      .def("Open", &SkinnyClient::Open,
           py::call_guard<py::gil_scoped_release>(), py::arg("path"),
           py::arg("cb") = std::nullopt)
      .def("OpenDir", &SkinnyClient::OpenDir,
           py::call_guard<py::gil_scoped_release>(), py::arg("path"),
           py::arg("cb") = std::nullopt)
      .def("Close", &SkinnyClient::Close,
           py::call_guard<py::gil_scoped_release>())
      .def("SetContent", &SkinnyClient::SetContent,
           py::call_guard<py::gil_scoped_release>())
      .def(
          "GetContent",
          [](SkinnyClient& sc, int fh) { return py::bytes(sc.GetContent(fh)); },
          py::call_guard<py::gil_scoped_release>())
      .def("TryAcquire", &SkinnyClient::TryAcquire,
           py::call_guard<py::gil_scoped_release>())
      .def("Acquire", &SkinnyClient::Acquire,
           py::call_guard<py::gil_scoped_release>())
      .def("Release", &SkinnyClient::Release,
           py::call_guard<py::gil_scoped_release>())
      .def("Delete", &SkinnyClient::Delete,
           py::call_guard<py::gil_scoped_release>());
  py::class_<SkinnyDiagnosticClient>(m, "SkinnyDiagnosticClient")
      .def(py::init())
      .def("GetLeader", &SkinnyDiagnosticClient::GetLeader);
}
