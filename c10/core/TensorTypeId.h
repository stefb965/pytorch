#pragma once

#include <iostream>
#include <string>
#include "c10/macros/Macros.h"

namespace c10 {

// A "bit" in a TensorTypeSet, which may have a unique dispatch handler
// for it.  Higher bit indexes get handled by dispatching first (because
// we "count leading zeros")
enum class TensorTypeId : uint8_t {
  // This is not a "real" tensor id, but it exists to give us
  // a "neutral" element we can return when a TensorTypeSet doesn't
  // give us any valid dispatch.
  UndefinedTensorId = 0,

  // This pool of IDs is not really ordered, but it is merged into
  // the hierarchy for convenience and performance
  CPUTensorId, // PyTorch/Caffe2 supported
  CUDATensorId, // PyTorch/Caffe2 supported
  SparseCPUTensorId, // PyTorch only
  SparseCUDATensorId, // PyTorch only
  MKLDNNTensorId, // Caffe2 only
  OpenGLTensorId, // Caffe2 only
  OpenCLTensorId, // Caffe2 only
  IDEEPTensorId, // Caffe2 only
  HIPTensorId, // PyTorch/Caffe2 supported
  SparseHIPTensorId, // PyTorch only
  MSNPUTensorId, // PyTorch only
  XLATensorId, // PyTorch only
  MkldnnCPUTensorId,
  QuantizedCPUTensorId, // PyTorch only
  ComplexCPUTensorId, // PyTorch only
  ComplexCUDATensorId, // PyTorch only

  VariableTensorId,

  NumTensorIds, // Sentinel
};

static_assert(
  static_cast<uint8_t>(TensorTypeId::NumTensorIds) < 64,
  "TensorTypeId is used as index into 64-bit bitmask; you must have less than 64 entries");

C10_API const char* toString(TensorTypeId);
C10_API std::ostream& operator<<(std::ostream&, TensorTypeId);

// For backwards compatibility with XLA repository
// (I don't want to fix this in XLA right now because there might be
// more renaming coming in the future.)
static inline TensorTypeId XLATensorId() {
  return TensorTypeId::XLATensorId;
}

} // namespace c10

// NB: You really shouldn't use this instance; this enum is guaranteed
// to be pretty small so a regular array should be acceptable.
namespace std {
template <>
struct hash<c10::TensorTypeId> {
  size_t operator()(c10::TensorTypeId x) const {
    return static_cast<size_t>(x);
  }
};
}
