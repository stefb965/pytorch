#include <torch/csrc/python_headers.h>

#include <c10d/FileStore.hpp>
#include <c10d/ProcessGroup.hpp>

#ifdef USE_C10D_GLOO
#include <c10d/ProcessGroupGloo.hpp>
#endif

#ifdef USE_C10D_NCCL
#include <c10d/ProcessGroupNCCL.hpp>
#endif

#ifdef USE_C10D_MPI
#include <c10d/ProcessGroupMPI.hpp>
#endif

#include <c10d/PrefixStore.hpp>
#include <c10d/TCPStore.hpp>
#include <gloo/transport/tcp/device.h>
#include <pybind11/chrono.h>

#include <torch/csrc/Exceptions.h>
#include <torch/csrc/distributed/c10d/comm.h>
#include <torch/csrc/distributed/c10d/ddp.h>
#include <torch/csrc/distributed/c10d/reducer.h>
#include <torch/csrc/utils/object_ptr.h>
#include <torch/csrc/utils/pybind.h>

namespace torch {
namespace distributed {
namespace c10d {

namespace {

#ifdef USE_C10D_GLOO
constexpr char* GLOO_SOCKET_IFNAME_ENV = "GLOO_SOCKET_IFNAME";

std::shared_ptr<::gloo::transport::Device> createDeviceForDefaultHostname() {
  ::gloo::transport::tcp::attr attr;

  // Use the hostname to resolve the network address to
  // use. Note: if the hostname does not resolve to an address (e.g.
  // because of misconfigured /etc/hosts file), this will not work.
  std::array<char, HOST_NAME_MAX> hostname{};
  auto rv = gethostname(hostname.data(), hostname.size());
  if (rv != 0) {
    throw std::system_error(errno, std::system_category());
  }
  attr.hostname = hostname.data();
  return ::gloo::transport::tcp::CreateDevice(attr);
}

std::shared_ptr<::gloo::transport::Device> createDeviceForInterface(
    std::string iface) {
  ::gloo::transport::tcp::attr attr;
  attr.iface = std::move(iface);
  return ::gloo::transport::tcp::CreateDevice(attr);
}
#endif

std::vector<std::string> split(char separator, const std::string& string) {
  std::vector<std::string> pieces;
  std::stringstream ss(string);
  std::string item;
  while (std::getline(ss, item, separator)) {
    pieces.push_back(std::move(item));
  }
  return pieces;
}

template <typename T>
using shared_ptr_class_ = py::class_<T, std::shared_ptr<T>>;

PyObject* c10d_init(PyObject* _unused) {
  C10_LOG_API_USAGE_ONCE("c10d.python.import");
  auto c10d_module = THPObjectPtr(PyImport_ImportModule("torch.distributed"));
  if (!c10d_module) {
    throw python_error();
  }

  auto module = py::handle(c10d_module).cast<py::module>();

  shared_ptr_class_<::c10d::Reducer>(module, "Reducer")
      .def(
          py::init<
              std::vector<std::vector<torch::autograd::Variable>>,
              std::vector<std::vector<size_t>>,
              std::shared_ptr<::c10d::ProcessGroup>,
              std::vector<std::vector<bool>>>(),
          py::arg("replicas"),
          py::arg("bucket_indices"),
          py::arg("process_group"),
          py::arg("expect_sparse_gradients") = std::vector<std::vector<bool>>())
      .def(
          "initialize_buckets",
          &::c10d::Reducer::initialize_buckets,
          py::call_guard<py::gil_scoped_release>())
      .def(
          "prepare_for_backward",
          &::c10d::Reducer::prepare_for_backward,
          py::call_guard<py::gil_scoped_release>())
      .def(
          "prepare_for_backward",
          [](::c10d::Reducer& reducer, const torch::autograd::Variable& output)
              -> void { reducer.prepare_for_backward({output}); },
          py::call_guard<py::gil_scoped_release>())
      .def("get_backward_stats", &::c10d::Reducer::get_backward_stats);

  py::enum_<::c10d::ReduceOp>(module, "ReduceOp", R"(
An enum-like class of available reduce operations: ``SUM``, ``PRODUCT``,
``MIN``, and ``MAX``.

The values of this class can be accessed as attributes, e.g., ``ReduceOp.SUM``.
They are used in specifying strategies for reduction collectives, e.g.,
:func:`reduce`, :func:`all_reduce_multigpu`, etc.)")
      .value("SUM", ::c10d::ReduceOp::SUM)
      .value("PRODUCT", ::c10d::ReduceOp::PRODUCT)
      .value("MIN", ::c10d::ReduceOp::MIN)
      .value("MAX", ::c10d::ReduceOp::MAX);

  py::class_<::c10d::BroadcastOptions>(module, "BroadcastOptions")
      .def(py::init<>())
      .def_readwrite("rootRank", &::c10d::BroadcastOptions::rootRank)
      .def_readwrite("rootTensor", &::c10d::BroadcastOptions::rootTensor)
      .def_readwrite("timeout", &::c10d::BroadcastOptions::timeout);

  py::class_<::c10d::AllreduceOptions>(module, "AllreduceOptions")
      .def(py::init<>())
      .def_readwrite("reduceOp", &::c10d::AllreduceOptions::reduceOp)
      .def_readwrite("timeout", &::c10d::AllreduceOptions::timeout);

  py::class_<::c10d::AllreduceCoalescedOptions>(
      module, "AllreduceCoalescedOptions")
      .def(py::init<>())
      .def_readwrite("reduceOp", &::c10d::AllreduceCoalescedOptions::reduceOp)
      .def_readwrite("timeout", &::c10d::AllreduceCoalescedOptions::timeout);

  py::class_<::c10d::ReduceOptions>(module, "ReduceOptions")
      .def(py::init<>())
      .def_readwrite("reduceOp", &::c10d::ReduceOptions::reduceOp)
      .def_readwrite("rootRank", &::c10d::ReduceOptions::rootRank)
      .def_readwrite("rootTensor", &::c10d::ReduceOptions::rootTensor)
      .def_readwrite("timeout", &::c10d::ReduceOptions::timeout);

  py::class_<::c10d::AllgatherOptions>(module, "AllgatherOptions")
      .def(py::init<>())
      .def_readwrite("timeout", &::c10d::AllgatherOptions::timeout);

  py::class_<::c10d::GatherOptions>(module, "GatherOptions")
      .def(py::init<>())
      .def_readwrite("rootRank", &::c10d::GatherOptions::rootRank)
      .def_readwrite("timeout", &::c10d::GatherOptions::timeout);

  py::class_<::c10d::ScatterOptions>(module, "ScatterOptions")
      .def(py::init<>())
      .def_readwrite("rootRank", &::c10d::ScatterOptions::rootRank)
      .def_readwrite("timeout", &::c10d::ScatterOptions::timeout);

  py::class_<::c10d::ReduceScatterOptions>(module, "ReduceScatterOptions")
      .def(py::init<>())
      .def_readwrite("reduceOp", &::c10d::ReduceScatterOptions::reduceOp)
      .def_readwrite("timeout", &::c10d::ReduceScatterOptions::timeout);

  py::class_<::c10d::BarrierOptions>(module, "BarrierOptions")
      .def(py::init<>())
      .def_readwrite("timeout", &::c10d::BarrierOptions::timeout);

  auto store =
      shared_ptr_class_<::c10d::Store>(module, "Store")
          // Convert from std::string to std::vector<uint8>.
          .def(
              "set",
              [](::c10d::Store& store,
                 const std::string& key,
                 const std::string& value) {
                std::vector<uint8_t> value_(value.begin(), value.end());
                store.set(key, value_);
              },
              py::call_guard<py::gil_scoped_release>())
          // Convert from std::vector<uint8_t> to py::bytes.
          // The returned value is not guaranteed to be valid UTF-8.
          .def(
              "get",
              [](::c10d::Store& store, const std::string& key) -> py::bytes {
                auto value = store.get(key);
                return py::bytes(
                    reinterpret_cast<char*>(value.data()), value.size());
              },
              py::call_guard<py::gil_scoped_release>())
          .def(
              "add",
              &::c10d::Store::add,
              py::call_guard<py::gil_scoped_release>())
          .def(
              "set_timeout",
              &::c10d::Store::setTimeout,
              py::call_guard<py::gil_scoped_release>())
          .def(
              "wait",
              [](::c10d::Store& store, const std::vector<std::string>& keys) {
                store.wait(keys);
              },
              py::call_guard<py::gil_scoped_release>())
          .def(
              "wait",
              [](::c10d::Store& store,
                 const std::vector<std::string>& keys,
                 const std::chrono::milliseconds& timeout) {
                store.wait(keys, timeout);
              },
              py::call_guard<py::gil_scoped_release>());

  shared_ptr_class_<::c10d::FileStore>(module, "FileStore", store)
      .def(py::init<const std::string&, int>());

  shared_ptr_class_<::c10d::TCPStore>(module, "TCPStore", store)
      .def(py::init<const std::string&, int, int, bool>());

  shared_ptr_class_<::c10d::PrefixStore>(module, "PrefixStore", store)
      .def(py::init<const std::string&, ::c10d::Store&>());

  auto processGroup =
      shared_ptr_class_<::c10d::ProcessGroup>(module, "ProcessGroup")
          .def("rank", &::c10d::ProcessGroup::getRank)
          .def("size", &::c10d::ProcessGroup::getSize)

          .def(
              "broadcast",
              &::c10d::ProcessGroup::broadcast,
              py::arg("tensors"),
              py::arg("opts") = ::c10d::BroadcastOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "broadcast",
              [](::c10d::ProcessGroup& pg, at::Tensor& x, int rootRank) {
                ::c10d::BroadcastOptions opts;
                opts.rootRank = rootRank;
                std::vector<at::Tensor> xs = {x};
                return pg.broadcast(xs, opts);
              },
              py::arg("tensor"),
              py::arg("root"),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "allreduce",
              &::c10d::ProcessGroup::allreduce,
              py::arg("tensors"),
              py::arg("opts") = ::c10d::AllreduceOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "allreduce",
              [](::c10d::ProcessGroup& pg,
                 std::vector<at::Tensor>& xs,
                 ::c10d::ReduceOp op) {
                ::c10d::AllreduceOptions opts;
                opts.reduceOp = op;
                return pg.allreduce(xs, opts);
              },
              py::arg("tensors"),
              py::arg("op") = ::c10d::ReduceOp::SUM,
              py::call_guard<py::gil_scoped_release>())

          .def(
              "allreduce",
              [](::c10d::ProcessGroup& pg, at::Tensor& x, ::c10d::ReduceOp op) {
                ::c10d::AllreduceOptions opts;
                opts.reduceOp = op;
                std::vector<at::Tensor> xs = {x};
                return pg.allreduce(xs, opts);
              },
              py::arg("tensor"),
              py::arg("op") = ::c10d::ReduceOp::SUM,
              py::call_guard<py::gil_scoped_release>())

          .def(
              "allreduce_coalesced",
              [](::c10d::ProcessGroup& pg,
                 std::vector<at::Tensor>& xs,
                 ::c10d::AllreduceCoalescedOptions opts) {
                return pg.allreduce_coalesced(xs, opts);
              },
              py::arg("tensors"),
              py::arg("opts") = ::c10d::AllreduceCoalescedOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "reduce",
              &::c10d::ProcessGroup::reduce,
              py::arg("tensors"),
              py::arg("opts") = ::c10d::ReduceOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "reduce",
              [](::c10d::ProcessGroup& pg,
                 at::Tensor& x,
                 int rootRank,
                 ::c10d::ReduceOp op) {
                ::c10d::ReduceOptions opts;
                opts.reduceOp = op;
                opts.rootRank = rootRank;
                std::vector<at::Tensor> xs = {x};
                return pg.reduce(xs, opts);
              },
              py::arg("tensor"),
              py::arg("root"),
              py::arg("op") = ::c10d::ReduceOp::SUM,
              py::call_guard<py::gil_scoped_release>())

          .def(
              "allgather",
              &::c10d::ProcessGroup::allgather,
              py::arg("output_tensors"),
              py::arg("input_tensors"),
              py::arg("opts") = ::c10d::AllgatherOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "allgather",
              [](::c10d::ProcessGroup& pg,
                 std::vector<at::Tensor>& output,
                 at::Tensor& input) {
                std::vector<std::vector<at::Tensor>> outputs = {output};
                std::vector<at::Tensor> inputs = {input};
                return pg.allgather(
                    outputs, inputs, ::c10d::AllgatherOptions());
              },
              py::arg("output_tensors"),
              py::arg("input_tensor"),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "gather",
              &::c10d::ProcessGroup::gather,
              py::arg("output_tensors"),
              py::arg("input_tensors"),
              py::arg("opts") = ::c10d::GatherOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "gather",
              [](::c10d::ProcessGroup& pg,
                 std::vector<at::Tensor>& output,
                 at::Tensor& input,
                 int rootRank) {
                ::c10d::GatherOptions opts;
                opts.rootRank = rootRank;
                std::vector<std::vector<at::Tensor>> outputs = {output};
                std::vector<at::Tensor> inputs = {input};
                return pg.gather(outputs, inputs, opts);
              },
              py::arg("output_tensors"),
              py::arg("input_tensor"),
              py::arg("root"),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "scatter",
              &::c10d::ProcessGroup::scatter,
              py::arg("output_tensors"),
              py::arg("input_tensors"),
              py::arg("opts") = ::c10d::ScatterOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "scatter",
              [](::c10d::ProcessGroup& pg,
                 at::Tensor& output,
                 std::vector<at::Tensor>& input,
                 int rootRank) {
                ::c10d::ScatterOptions opts;
                opts.rootRank = rootRank;
                std::vector<std::vector<at::Tensor>> inputs = {input};
                std::vector<at::Tensor> outputs = {output};
                return pg.scatter(outputs, inputs, opts);
              },
              py::arg("output_tensor"),
              py::arg("input_tensors"),
              py::arg("root"),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "reduce_scatter",
              &::c10d::ProcessGroup::reduce_scatter,
              py::arg("output_tensors"),
              py::arg("input_tensors"),
              py::arg("opts") = ::c10d::ReduceScatterOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "reduce_scatter",
              [](::c10d::ProcessGroup& pg,
                 at::Tensor& output,
                 std::vector<at::Tensor>& input) {
                std::vector<at::Tensor> outputs = {output};
                std::vector<std::vector<at::Tensor>> inputs = {input};
                return pg.reduce_scatter(
                    outputs, inputs, ::c10d::ReduceScatterOptions());
              },
              py::arg("output_tensors"),
              py::arg("input_tensor"),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "send",
              &::c10d::ProcessGroup::send,
              py::call_guard<py::gil_scoped_release>())

          .def(
              "recv",
              &::c10d::ProcessGroup::recv,
              py::call_guard<py::gil_scoped_release>())

          .def(
              "recv_anysource",
              &::c10d::ProcessGroup::recvAnysource,
              py::call_guard<py::gil_scoped_release>())

          .def(
              "abort",
              &::c10d::ProcessGroup::barrier,
              py::arg("opts") = ::c10d::BarrierOptions(),
              py::call_guard<py::gil_scoped_release>())

          .def(
              "barrier",
              &::c10d::ProcessGroup::barrier,
              py::arg("opts") = ::c10d::BarrierOptions(),
              py::call_guard<py::gil_scoped_release>());

#ifdef USE_C10D_GLOO
  auto processGroupGloo = shared_ptr_class_<::c10d::ProcessGroupGloo>(
      module, "ProcessGroupGloo", processGroup);

  shared_ptr_class_<::gloo::transport::Device>(processGroupGloo, "Device");

  shared_ptr_class_<::c10d::ProcessGroupGloo::Options>(
      processGroupGloo, "Options")
      .def(py::init<>())
      .def_readwrite("devices", &::c10d::ProcessGroupGloo::Options::devices)
      .def_readwrite("timeout", &::c10d::ProcessGroupGloo::Options::timeout)
      .def_readwrite("threads", &::c10d::ProcessGroupGloo::Options::threads);

  processGroupGloo.def_static(
      "create_tcp_device",
      [](const std::string& hostname, const std::string& interface)
          -> std::shared_ptr<::gloo::transport::Device> {
        ::gloo::transport::tcp::attr attr;
        if (!hostname.empty()) {
          attr.hostname = hostname;
        } else if (!interface.empty()) {
          attr.iface = interface;
        } else {
          // Neither argument is specified; Gloo itself will use the
          // hostname
          // Nothing specified, default to something useful
        }
        return ::gloo::transport::tcp::CreateDevice(attr);
      },
      py::arg("hostname") = "",
      py::arg("interface") = "");

  processGroupGloo
      .def(py::init<
           const std::shared_ptr<::c10d::Store>&,
           int,
           int,
           ::c10d::ProcessGroupGloo::Options>())
      .def(
          py::init([](const std::shared_ptr<::c10d::Store>& store,
                      int rank,
                      int size,
                      std::chrono::milliseconds timeout) {
            ::c10d::ProcessGroupGloo::Options options;

            // Use interfaces listed in "GLOO_SOCKET_IFNAME", if set.
            char* ifnameEnv = getenv(GLOO_SOCKET_IFNAME_ENV);
            if (ifnameEnv) {
              for (const auto& iface : split(',', ifnameEnv)) {
                options.devices.push_back(createDeviceForInterface(iface));
              }
            } else {
              options.devices.push_back(createDeviceForDefaultHostname());
            }

            options.timeout = timeout;
            options.threads = options.devices.size() * 2;
            return std::make_shared<::c10d::ProcessGroupGloo>(
                store, rank, size, options);
          }),
          py::arg("store"),
          py::arg("rank"),
          py::arg("size"),
          py::arg("timeout") = std::chrono::milliseconds(10 * 1000));
#endif

#ifdef USE_C10D_NCCL
  shared_ptr_class_<::c10d::ProcessGroupNCCL>(
      module, "ProcessGroupNCCL", processGroup)
      .def(
          py::init<
              const std::shared_ptr<::c10d::Store>&,
              int,
              int,
              const std::string&,
              const std::chrono::milliseconds&>(),
          py::arg("store"),
          py::arg("rank"),
          py::arg("size"),
          py::arg("groupName") = "",
          py::arg("timeout") = std::chrono::milliseconds(
              ::c10d::ProcessGroupNCCL::kProcessGroupNCCLOpTimeoutMillis));
#endif

#ifdef USE_C10D_MPI
  auto processGroupMPI = shared_ptr_class_<::c10d::ProcessGroupMPI>(
      module, "ProcessGroupMPI", processGroup);

  // Define static create function instead of a constructor, because
  // this function may return null. This happens if this process is not
  // part of a sub group that is to be created.
  processGroupMPI.def_static("create", [](std::vector<int> ranks) {
    return ::c10d::ProcessGroupMPI::createProcessGroupMPI(ranks);
  });
#endif

  shared_ptr_class_<::c10d::ProcessGroup::Work>(module, "Work")
      .def("is_completed", &::c10d::ProcessGroup::Work::isCompleted)
      .def("is_success", &::c10d::ProcessGroup::Work::isSuccess)
      .def("exception", &::c10d::ProcessGroup::Work::exception)
      .def("source_rank", &::c10d::ProcessGroup::Work::sourceRank)
      .def(
          "result",
          [](::c10d::ProcessGroup::Work& work) -> std::vector<at::Tensor> {
            auto tensors = work.result();
            for (auto& tensor : tensors) {
              tensor = autograd::make_variable(tensor);
            }
            return tensors;
          })
      .def("synchronize", &::c10d::ProcessGroup::Work::synchronize)
      .def(
          "wait",
          &::c10d::ProcessGroup::Work::wait,
          py::call_guard<py::gil_scoped_release>());

#ifdef USE_CUDA
  module.def(
      "_dist_bucket_tensors",
      &::c10d::bucketTensors,
      py::arg("tensors"),
      py::arg("bucket_size"),
      py::arg("fine_grained"),
      py::call_guard<py::gil_scoped_release>());

  module.def(
      "_dist_broadcast_coalesced",
      &::c10d::distBroadcastCoalesced,
      py::arg("process_group"),
      py::arg("tensors"),
      py::arg("buffer_size"),
      py::arg("fine_grained"),
      py::call_guard<py::gil_scoped_release>());

  module.def(
      "_sync_params",
      &::c10d::syncParams,
      py::arg("process_group"),
      py::arg("parameter_data"),
      py::arg("buffer_data"),
      py::arg("devices"),
      py::arg("broadcast_bucket_size"),
      py::arg("broadcast_buffers"),
      py::call_guard<py::gil_scoped_release>());

  module.def(
      "_queue_reduction",
      &::c10d::queueReduction,
      py::arg("process_group"),
      py::arg("grads_batch"),
      py::arg("devices"),
      py::call_guard<py::gil_scoped_release>());

  module.def(
      "_sync_reduction",
      &::c10d::syncReduction,
      py::arg("reduction_work"),
      py::arg("grads_batch"),
      py::arg("grads_batch_coalesced"),
      py::call_guard<py::gil_scoped_release>());
#endif

  module.def(
      "_compute_bucket_assignment_by_size",
      &::c10d::compute_bucket_assignment_by_size,
      py::arg("tensors"),
      py::arg("bucket_size"),
      py::arg("expect_sparse_gradient") = std::vector<bool>(),
      py::call_guard<py::gil_scoped_release>());

  module.def(
      "_broadcast_coalesced",
      // Define a lambda such that the pybind11 prototype can take a std::vector
      // for the tensor list argument, but still pass it to the underlying
      // function as a c10::ArrayRef.
      [](std::shared_ptr<::c10d::ProcessGroup> process_group,
         std::vector<at::Tensor> tensors,
         size_t buffer_size) {
        broadcast_coalesced(process_group, tensors, buffer_size);
      },
      py::arg("process_group"),
      py::arg("tensors"),
      py::arg("buffer_size"),
      py::call_guard<py::gil_scoped_release>());

  Py_RETURN_TRUE;
}

} // namespace

// c10d methods on torch._C
static PyMethodDef methods[] = {
    {"_c10d_init", (PyCFunction)c10d_init, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};

PyMethodDef* python_functions() {
  return methods;
}

} // namespace c10d
} // namespace distributed
} // namespace torch
