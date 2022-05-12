#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <optional>

#include "clientlib.h"
#include "clientlib_diagnostic.h"

namespace py = pybind11;

PYBIND11_MODULE(pyclientlib, m) {
  py::class_<SkinnyClient>(m, "SkinnyClient")
      .def(py::init(), py::call_guard<py::gil_scoped_release>())
      .def(
          "Open",
          [](SkinnyClient& sc, std::string& path,
             std::optional<std::function<void(int)>>& cb, bool is_ephemeral) {
            if (cb) {
              return sc.Open(
                  path,
                  [cb](int fh) {
                    py::gil_scoped_acquire acquire;
                    std::invoke(cb.value(), fh);
                  },
                  is_ephemeral);
            } else {
              return sc.Open(path, std::nullopt, is_ephemeral);
            }
          },
          py::call_guard<py::gil_scoped_release>(), py::arg("path"),
          py::arg("cb") = std::nullopt, py::arg("is_ephemeral") = false)
      .def(
          "OpenDir",
          [](SkinnyClient& sc, std::string& path,
             std::optional<std::function<void(int)>>& cb, bool is_ephemeral) {
            if (cb) {
              return sc.OpenDir(
                  path,
                  [cb](int fh) {
                    py::gil_scoped_acquire acquire;
                    std::invoke(cb.value(), fh);
                  },
                  is_ephemeral);
            } else {
              return sc.OpenDir(path, std::nullopt, is_ephemeral);
            }
          },
          py::call_guard<py::gil_scoped_release>(), py::arg("path"),
          py::arg("cb") = std::nullopt, py::arg("is_ephemeral") = false)
      .def("Close", &SkinnyClient::Close,
           py::call_guard<py::gil_scoped_release>())
      .def("SetContent", &SkinnyClient::SetContent,
           py::call_guard<py::gil_scoped_release>())
      .def(
          "GetContent",
          [](SkinnyClient& sc, int fh) {
            std::string result = sc.GetContent(fh);
            {
              py::gil_scoped_acquire acquire;
              return py::bytes(result);
            }
          },
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
