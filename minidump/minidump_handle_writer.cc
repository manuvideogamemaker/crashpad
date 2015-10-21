// Copyright 2015 The Crashpad Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "minidump/minidump_handle_writer.h"

#include <string>

#include "base/logging.h"
#include "minidump/minidump_extensions.h"
#include "util/file/file_writer.h"

namespace crashpad {

MinidumpHandleDataWriter::MinidumpHandleDataWriter()
    : handle_data_stream_base_(), handle_descriptors_(), strings_() {
}

MinidumpHandleDataWriter::~MinidumpHandleDataWriter() {
}

void MinidumpHandleDataWriter::InitializeFromSnapshot(
    const std::vector<HandleSnapshot>& handle_snapshots) {
  DCHECK_EQ(state(), kStateMutable);

  DCHECK(handle_descriptors_.empty());
  // Because we RegisterRVA() on the string writer below, we preallocate and
  // never resize the handle_descriptors_ vector.
  handle_descriptors_.resize(handle_snapshots.size());
  strings_.reserve(handle_snapshots.size());
  for (size_t i = 0; i < handle_snapshots.size(); ++i) {
    const HandleSnapshot& handle_snapshot = handle_snapshots[i];
    MINIDUMP_HANDLE_DESCRIPTOR& descriptor = handle_descriptors_[i];

    descriptor.Handle = handle_snapshot.handle;

    if (handle_snapshot.type_name.empty()) {
      descriptor.TypeNameRva = 0;
    } else {
      // TODO(scottmg): There is often a number of repeated type names here, the
      // strings ought to be pooled.
      strings_.push_back(new internal::MinidumpUTF16StringWriter());
      strings_.back()->SetUTF16(handle_snapshot.type_name);
      strings_.back()->RegisterRVA(&descriptor.TypeNameRva);
    }

    descriptor.ObjectNameRva = 0;
    descriptor.Attributes = handle_snapshot.attributes;
    descriptor.GrantedAccess = handle_snapshot.granted_access;
    descriptor.HandleCount = handle_snapshot.handle_count;
    descriptor.PointerCount = handle_snapshot.pointer_count;
  }
}

bool MinidumpHandleDataWriter::Freeze() {
  DCHECK_EQ(state(), kStateMutable);

  if (!MinidumpStreamWriter::Freeze())
    return false;

  handle_data_stream_base_.SizeOfHeader = sizeof(handle_data_stream_base_);
  handle_data_stream_base_.SizeOfDescriptor = sizeof(handle_descriptors_[0]);
  handle_data_stream_base_.NumberOfDescriptors = handle_descriptors_.size();
  handle_data_stream_base_.Reserved = 0;

  return true;
}

size_t MinidumpHandleDataWriter::SizeOfObject() {
  DCHECK_GE(state(), kStateFrozen);
  return sizeof(handle_data_stream_base_) +
         sizeof(handle_descriptors_[0]) * handle_descriptors_.size();
}

std::vector<internal::MinidumpWritable*> MinidumpHandleDataWriter::Children() {
  DCHECK_GE(state(), kStateFrozen);

  std::vector<MinidumpWritable*> children;
  for (auto* string : strings_)
    children.push_back(string);
  return children;
}

bool MinidumpHandleDataWriter::WriteObject(FileWriterInterface* file_writer) {
  DCHECK_EQ(state(), kStateWritable);

  WritableIoVec iov;
  iov.iov_base = &handle_data_stream_base_;
  iov.iov_len = sizeof(handle_data_stream_base_);
  std::vector<WritableIoVec> iovecs(1, iov);

  for (const auto& descriptor : handle_descriptors_) {
    iov.iov_base = &descriptor;
    iov.iov_len = sizeof(descriptor);
    iovecs.push_back(iov);
  }

  return file_writer->WriteIoVec(&iovecs);
}

MinidumpStreamType MinidumpHandleDataWriter::StreamType() const {
  return kMinidumpStreamTypeHandleData;
}

}  // namespace crashpad