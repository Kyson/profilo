/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_OAT_QUICK_METHOD_HEADER_H_
#define ART_RUNTIME_OAT_QUICK_METHOD_HEADER_H_

#include "arch/instruction_set.h"
#include "base/macros.h"
#include "quick/quick_method_frame_info.h"
#include "method_info.h"
#include "stack_map.h"
#include "utils.h"

namespace art {

class ArtMethod;

// OatQuickMethodHeader precedes the raw code chunk generated by the compiler.
class PACKED(4) OatQuickMethodHeader {
 public:
  OatQuickMethodHeader() = default;
  explicit OatQuickMethodHeader(uint32_t vmap_table_offset,
                                uint32_t method_info_offset,
                                uint32_t frame_size_in_bytes,
                                uint32_t core_spill_mask,
                                uint32_t fp_spill_mask,
                                uint32_t code_size);

  ~OatQuickMethodHeader();

  static OatQuickMethodHeader* FromCodePointer(const void* code_ptr) {
    uintptr_t code = reinterpret_cast<uintptr_t>(code_ptr);
    uintptr_t header = code - OFFSETOF_MEMBER(OatQuickMethodHeader, code_);
    DCHECK(IsAlignedParam(code, GetInstructionSetAlignment(kRuntimeISA)) ||
           IsAlignedParam(header, GetInstructionSetAlignment(kRuntimeISA)))
        << std::hex << code << " " << std::hex << header;
    return reinterpret_cast<OatQuickMethodHeader*>(header);
  }

  static OatQuickMethodHeader* FromEntryPoint(const void* entry_point) {
    return FromCodePointer(EntryPointToCodePointer(entry_point));
  }

  OatQuickMethodHeader& operator=(const OatQuickMethodHeader&) = default;

  uintptr_t NativeQuickPcOffset(const uintptr_t pc) const {
    return pc - reinterpret_cast<uintptr_t>(GetEntryPoint());
  }

  bool IsOptimized() const {
    return GetCodeSize() != 0 && vmap_table_offset_ != 0;
  }

  const void* GetOptimizedCodeInfoPtr() const {
    DCHECK(IsOptimized());
    return reinterpret_cast<const void*>(code_ - vmap_table_offset_);
  }

  uint8_t* GetOptimizedCodeInfoPtr() {
    DCHECK(IsOptimized());
    return code_ - vmap_table_offset_;
  }

  CodeInfo GetOptimizedCodeInfo() const {
    return CodeInfo(GetOptimizedCodeInfoPtr());
  }

  const void* GetOptimizedMethodInfoPtr() const {
    DCHECK(IsOptimized());
    return reinterpret_cast<const void*>(code_ - method_info_offset_);
  }

  uint8_t* GetOptimizedMethodInfoPtr() {
    DCHECK(IsOptimized());
    return code_ - method_info_offset_;
  }

  MethodInfo GetOptimizedMethodInfo() const {
    return MethodInfo(reinterpret_cast<const uint8_t*>(GetOptimizedMethodInfoPtr()));
  }

  const uint8_t* GetCode() const {
    return code_;
  }

  uint32_t GetCodeSize() const {
    return code_size_ & kCodeSizeMask;
  }

  const uint32_t* GetCodeSizeAddr() const {
    return &code_size_;
  }

  uint32_t GetVmapTableOffset() const {
    return vmap_table_offset_;
  }

  void SetVmapTableOffset(uint32_t offset) {
    vmap_table_offset_ = offset;
  }

  const uint32_t* GetVmapTableOffsetAddr() const {
    return &vmap_table_offset_;
  }

  uint32_t GetMethodInfoOffset() const {
    return method_info_offset_;
  }

  void SetMethodInfoOffset(uint32_t offset) {
    method_info_offset_ = offset;
  }

  const uint32_t* GetMethodInfoOffsetAddr() const {
    return &method_info_offset_;
  }

  const uint8_t* GetVmapTable() const {
    CHECK(!IsOptimized()) << "Unimplemented vmap table for optimizing compiler";
    return (vmap_table_offset_ == 0) ? nullptr : code_ - vmap_table_offset_;
  }

  bool Contains(uintptr_t pc) const {
    uintptr_t code_start = reinterpret_cast<uintptr_t>(code_);
    static_assert(kRuntimeISA != kThumb2, "kThumb2 cannot be a runtime ISA");
    if (kRuntimeISA == kArm) {
      // On Thumb-2, the pc is offset by one.
      code_start++;
    }
    return code_start <= pc && pc <= (code_start + GetCodeSize());
  }

  const uint8_t* GetEntryPoint() const {
    // When the runtime architecture is ARM, `kRuntimeISA` is set to `kArm`
    // (not `kThumb2`), *but* we always generate code for the Thumb-2
    // instruction set anyway. Thumb-2 requires the entrypoint to be of
    // offset 1.
    static_assert(kRuntimeISA != kThumb2, "kThumb2 cannot be a runtime ISA");
    return (kRuntimeISA == kArm)
        ? reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(code_) | 1)
        : code_;
  }

  template <bool kCheckFrameSize = true>
  uint32_t GetFrameSizeInBytes() const {
    uint32_t result = frame_info_.FrameSizeInBytes();
    if (kCheckFrameSize) {
      DCHECK_ALIGNED(result, kStackAlignment);
    }
    return result;
  }

  QuickMethodFrameInfo GetFrameInfo() const {
    return frame_info_;
  }

  uintptr_t ToNativeQuickPc(ArtMethod* method,
                            const uint32_t dex_pc,
                            bool is_for_catch_handler,
                            bool abort_on_failure = true) const;

  uint32_t ToDexPc(ArtMethod* method, const uintptr_t pc, bool abort_on_failure = true) const;

  void SetHasShouldDeoptimizeFlag() {
    DCHECK_EQ(code_size_ & kShouldDeoptimizeMask, 0u);
    code_size_ |= kShouldDeoptimizeMask;
  }

  bool HasShouldDeoptimizeFlag() const {
    return (code_size_ & kShouldDeoptimizeMask) != 0;
  }

 private:
  static constexpr uint32_t kShouldDeoptimizeMask = 0x80000000;
  static constexpr uint32_t kCodeSizeMask = ~kShouldDeoptimizeMask;

  // The offset in bytes from the start of the vmap table to the end of the header.
  uint32_t vmap_table_offset_ = 0u;
  // The offset in bytes from the start of the method info to the end of the header.
  // The method info offset is not in the CodeInfo since CodeInfo has good dedupe properties that
  // would be lost from doing so. The method info memory region contains method indices since they
  // are hard to dedupe.
  uint32_t method_info_offset_ = 0u;
  // The stack frame information.
  QuickMethodFrameInfo frame_info_;
  // The code size in bytes. The highest bit is used to signify if the compiled
  // code with the method header has should_deoptimize flag.
  uint32_t code_size_ = 0u;
  // The actual code.
  uint8_t code_[0];
};

}  // namespace art

#endif  // ART_RUNTIME_OAT_QUICK_METHOD_HEADER_H_