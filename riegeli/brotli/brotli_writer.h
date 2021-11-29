// Copyright 2017 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RIEGELI_BROTLI_BROTLI_WRITER_H_
#define RIEGELI_BROTLI_BROTLI_WRITER_H_

#include <stddef.h>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "brotli/encode.h"
#include "riegeli/base/base.h"
#include "riegeli/base/dependency.h"
#include "riegeli/base/object.h"
#include "riegeli/brotli/brotli_allocator.h"
#include "riegeli/brotli/brotli_dictionary.h"
#include "riegeli/bytes/buffered_writer.h"
#include "riegeli/bytes/reader.h"
#include "riegeli/bytes/writer.h"

namespace riegeli {

template <typename Src>
class BrotliReader;

// Template parameter independent part of `BrotliWriter`.
class BrotliWriterBase : public BufferedWriter {
 public:
  class Options {
   public:
    Options() noexcept {}

    // Tunes the tradeoff between compression density and compression speed
    // (higher = better density but slower).
    //
    // `compression_level` must be between `kMinCompressionLevel` (0) and
    // `kMaxCompressionLevel` (11). Default: `kDefaultCompressionLevel` (6).
    static constexpr int kMinCompressionLevel = BROTLI_MIN_QUALITY;
    static constexpr int kMaxCompressionLevel = BROTLI_MAX_QUALITY;
    static constexpr int kDefaultCompressionLevel = 6;
    Options& set_compression_level(int compression_level) & {
      RIEGELI_ASSERT_GE(compression_level, kMinCompressionLevel)
          << "Failed precondition of "
             "BrotliWriterBase::Options::set_compression_level(): "
             "compression level out of range";
      RIEGELI_ASSERT_LE(compression_level, kMaxCompressionLevel)
          << "Failed precondition of "
             "BrotliWriterBase::Options::set_compression_level(): "
             "compression level out of range";
      compression_level_ = compression_level;
      return *this;
    }
    Options&& set_compression_level(int compression_level) && {
      return std::move(set_compression_level(compression_level));
    }
    int compression_level() const { return compression_level_; }

    // Logarithm of the LZ77 sliding window size. This tunes the tradeoff
    // between compression density and memory usage (higher = better density but
    // more memory).
    //
    // `window_log` must be between `kMinWindowLog` (10) and `kMaxWindowLog`
    // (30). Default: `kDefaultWindowLog` (22).
    static constexpr int kMinWindowLog = BROTLI_MIN_WINDOW_BITS;
    static constexpr int kMaxWindowLog = BROTLI_LARGE_MAX_WINDOW_BITS;
    static constexpr int kDefaultWindowLog = BROTLI_DEFAULT_WINDOW;
    Options& set_window_log(int window_log) & {
      RIEGELI_ASSERT_GE(window_log, kMinWindowLog)
          << "Failed precondition of "
             "BrotliWriterBase::Options::set_window_log(): "
             "window log out of range";
      RIEGELI_ASSERT_LE(window_log, kMaxWindowLog)
          << "Failed precondition of "
             "BrotliWriterBase::Options::set_window_log(): "
             "window log out of range";
      window_log_ = window_log;
      return *this;
    }
    Options&& set_window_log(int window_log) && {
      return std::move(set_window_log(window_log));
    }
    int window_log() const { return window_log_; }

    // Shared Brotli dictionary. The same dictionary must have been used for
    // compression.
    //
    // Default: `BrotliDictionary()`.
    Options& set_dictionary(const BrotliDictionary& dictionary) & {
      dictionary_ = dictionary;
      return *this;
    }
    Options& set_dictionary(BrotliDictionary&& dictionary) & {
      dictionary_ = std::move(dictionary);
      return *this;
    }
    Options&& set_dictionary(const BrotliDictionary& dictionary) && {
      return std::move(set_dictionary(dictionary));
    }
    Options&& set_dictionary(BrotliDictionary&& dictionary) && {
      return std::move(set_dictionary(std::move(dictionary)));
    }
    BrotliDictionary& dictionary() { return dictionary_; }
    const BrotliDictionary& dictionary() const { return dictionary_; }

    // Memory allocator used by the Brotli engine.
    //
    // Default: `BrotliAllocator()`.
    Options& set_allocator(const BrotliAllocator& allocator) & {
      allocator_ = allocator;
      return *this;
    }
    Options& set_allocator(BrotliAllocator&& allocator) & {
      allocator_ = std::move(allocator);
      return *this;
    }
    Options&& set_allocator(const BrotliAllocator& allocator) && {
      return std::move(set_allocator(allocator));
    }
    Options&& set_allocator(BrotliAllocator&& allocator) && {
      return std::move(set_allocator(std::move(allocator)));
    }
    BrotliAllocator& allocator() & { return allocator_; }
    const BrotliAllocator& allocator() const& { return allocator_; }
    BrotliAllocator&& allocator() && { return std::move(allocator_); }
    const BrotliAllocator&& allocator() const&& {
      return std::move(allocator_);
    }

    // Expected uncompressed size, or `absl::nullopt` if unknown. This may
    // improve compression density and performance.
    //
    // If the size hint turns out to not match reality, nothing breaks.
    //
    // Default: `absl::nullopt`.
    Options& set_size_hint(absl::optional<Position> size_hint) & {
      size_hint_ = size_hint;
      return *this;
    }
    Options&& set_size_hint(absl::optional<Position> size_hint) && {
      return std::move(set_size_hint(size_hint));
    }
    absl::optional<Position> size_hint() const { return size_hint_; }

    // Tunes how much data is buffered before calling the compression engine.
    //
    // Default: `kDefaultBufferSize` (64K).
    Options& set_buffer_size(size_t buffer_size) & {
      RIEGELI_ASSERT_GT(buffer_size, 0u)
          << "Failed precondition of "
             "BrotliWriterBase::Options::set_buffer_size(): "
             "zero buffer size";
      buffer_size_ = buffer_size;
      return *this;
    }
    Options&& set_buffer_size(size_t buffer_size) && {
      return std::move(set_buffer_size(buffer_size));
    }
    size_t buffer_size() const { return buffer_size_; }

   private:
    int compression_level_ = kDefaultCompressionLevel;
    int window_log_ = kDefaultWindowLog;
    BrotliDictionary dictionary_;
    BrotliAllocator allocator_;
    absl::optional<Position> size_hint_;
    size_t buffer_size_ = kDefaultBufferSize;
  };

  // Returns the compressed `Writer`. Unchanged by `Close()`.
  virtual Writer* dest_writer() = 0;
  virtual const Writer* dest_writer() const = 0;

  bool SupportsReadMode() override;

 protected:
  explicit BrotliWriterBase(Closed) noexcept : BufferedWriter(kClosed) {}

  explicit BrotliWriterBase(BrotliDictionary&& dictionary,
                            BrotliAllocator&& allocator, size_t buffer_size,
                            absl::optional<Position> size_hint);

  BrotliWriterBase(BrotliWriterBase&& that) noexcept;
  BrotliWriterBase& operator=(BrotliWriterBase&& that) noexcept;

  void Reset(Closed);
  void Reset(BrotliDictionary&& dictionary, BrotliAllocator&& allocator,
             size_t buffer_size, absl::optional<Position> size_hint);
  void Initialize(Writer* dest, int compression_level, int window_log,
                  absl::optional<Position> size_hint);

  void DoneBehindBuffer(absl::string_view src) override;
  void Done() override;
  // `BrotliWriterBase` overrides `Writer::AnnotateStatusImpl()` to annotate the
  // status with the current position, clarifying that this is the uncompressed
  // position. A status propagated from `*dest_writer()` might carry annotation
  // with the compressed position.
  ABSL_ATTRIBUTE_COLD absl::Status AnnotateStatusImpl(
      absl::Status status) override;
  bool WriteInternal(absl::string_view src) override;
  bool FlushBehindBuffer(absl::string_view src, FlushType flush_type);
  Reader* ReadModeBehindBuffer(Position initial_pos) override;

 private:
  struct BrotliEncoderStateDeleter {
    void operator()(BrotliEncoderState* ptr) const {
      BrotliEncoderDestroyInstance(ptr);
    }
  };

  bool WriteInternal(absl::string_view src, Writer& dest,
                     BrotliEncoderOperation op);

  BrotliDictionary dictionary_;
  BrotliAllocator allocator_;
  Position initial_compressed_pos_ = 0;
  std::unique_ptr<BrotliEncoderState, BrotliEncoderStateDeleter> compressor_;

  AssociatedReader<BrotliReader<Reader*>> associated_reader_;
};

// A `Writer` which compresses data with Brotli before passing it to another
// `Writer`.
//
// The `Dest` template parameter specifies the type of the object providing and
// possibly owning the compressed `Writer`. `Dest` must support
// `Dependency<Writer*, Dest>`, e.g. `Writer*` (not owned, default),
// `std::unique_ptr<Writer>` (owned), `ChainWriter<>` (owned).
//
// By relying on CTAD the template argument can be deduced as the value type of
// the first constructor argument. This requires C++17.
//
// The compressed `Writer` must not be accessed until the `BrotliWriter` is
// closed or no longer used, except that it is allowed to read the destination
// of the compressed `Writer` immediately after `Flush()`.
template <typename Dest = Writer*>
class BrotliWriter : public BrotliWriterBase {
 public:
  // Creates a closed `BrotliWriter`.
  explicit BrotliWriter(Closed) noexcept : BrotliWriterBase(kClosed) {}

  // Will write to the compressed `Writer` provided by `dest`.
  explicit BrotliWriter(const Dest& dest, Options options = Options());
  explicit BrotliWriter(Dest&& dest, Options options = Options());

  // Will write to the compressed `Writer` provided by a `Dest` constructed from
  // elements of `dest_args`. This avoids constructing a temporary `Dest` and
  // moving from it.
  template <typename... DestArgs>
  explicit BrotliWriter(std::tuple<DestArgs...> dest_args,
                        Options options = Options());

  BrotliWriter(BrotliWriter&& that) noexcept;
  BrotliWriter& operator=(BrotliWriter&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `BrotliWriter`. This avoids
  // constructing a temporary `BrotliWriter` and moving from it.
  void Reset(Closed);
  void Reset(const Dest& dest, Options options = Options());
  void Reset(Dest&& dest, Options options = Options());
  template <typename... DestArgs>
  void Reset(std::tuple<DestArgs...> dest_args, Options options = Options());

  // Returns the object providing and possibly owning the compressed `Writer`.
  // Unchanged by `Close()`.
  Dest& dest() { return dest_.manager(); }
  const Dest& dest() const { return dest_.manager(); }
  Writer* dest_writer() override { return dest_.get(); }
  const Writer* dest_writer() const override { return dest_.get(); }

 protected:
  void Done() override;
  bool FlushImpl(FlushType flush_type) override;

 private:
  // The object providing and possibly owning the compressed `Writer`.
  Dependency<Writer*, Dest> dest_;
};

// Support CTAD.
#if __cpp_deduction_guides
explicit BrotliWriter(Closed)->BrotliWriter<DeleteCtad<Closed>>;
template <typename Dest>
explicit BrotliWriter(const Dest& dest, BrotliWriterBase::Options options =
                                            BrotliWriterBase::Options())
    -> BrotliWriter<std::decay_t<Dest>>;
template <typename Dest>
explicit BrotliWriter(Dest&& dest, BrotliWriterBase::Options options =
                                       BrotliWriterBase::Options())
    -> BrotliWriter<std::decay_t<Dest>>;
template <typename... DestArgs>
explicit BrotliWriter(
    std::tuple<DestArgs...> dest_args,
    BrotliWriterBase::Options options = BrotliWriterBase::Options())
    -> BrotliWriter<DeleteCtad<std::tuple<DestArgs...>>>;
#endif

// Implementation details follow.

inline BrotliWriterBase::BrotliWriterBase(BrotliDictionary&& dictionary,
                                          BrotliAllocator&& allocator,
                                          size_t buffer_size,
                                          absl::optional<Position> size_hint)
    : BufferedWriter(buffer_size, size_hint),
      dictionary_(std::move(dictionary)),
      allocator_(std::move(allocator)) {}

inline BrotliWriterBase::BrotliWriterBase(BrotliWriterBase&& that) noexcept
    : BufferedWriter(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      dictionary_(std::move(that.dictionary_)),
      allocator_(std::move(that.allocator_)),
      initial_compressed_pos_(that.initial_compressed_pos_),
      compressor_(std::move(that.compressor_)),
      associated_reader_(std::move(that.associated_reader_)) {}

inline BrotliWriterBase& BrotliWriterBase::operator=(
    BrotliWriterBase&& that) noexcept {
  BufferedWriter::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  dictionary_ = std::move(that.dictionary_);
  allocator_ = std::move(that.allocator_);
  initial_compressed_pos_ = that.initial_compressed_pos_;
  compressor_ = std::move(that.compressor_);
  associated_reader_ = std::move(that.associated_reader_);
  return *this;
}

inline void BrotliWriterBase::Reset(Closed) {
  BufferedWriter::Reset(kClosed);
  initial_compressed_pos_ = 0;
  compressor_.reset();
  dictionary_ = BrotliDictionary();
  allocator_ = BrotliAllocator();
  associated_reader_.Reset();
}

inline void BrotliWriterBase::Reset(BrotliDictionary&& dictionary,
                                    BrotliAllocator&& allocator,
                                    size_t buffer_size,
                                    absl::optional<Position> size_hint) {
  BufferedWriter::Reset(buffer_size, size_hint);
  initial_compressed_pos_ = 0;
  compressor_.reset();
  dictionary_ = std::move(dictionary);
  allocator_ = std::move(allocator);
  associated_reader_.Reset();
}

template <typename Dest>
inline BrotliWriter<Dest>::BrotliWriter(const Dest& dest, Options options)
    : BrotliWriterBase(std::move(options.dictionary()),
                       std::move(options.allocator()), options.buffer_size(),
                       options.size_hint()),
      dest_(dest) {
  Initialize(dest_.get(), options.compression_level(), options.window_log(),
             options.size_hint());
}

template <typename Dest>
inline BrotliWriter<Dest>::BrotliWriter(Dest&& dest, Options options)
    : BrotliWriterBase(std::move(options.dictionary()),
                       std::move(options.allocator()), options.buffer_size(),
                       options.size_hint()),
      dest_(std::move(dest)) {
  Initialize(dest_.get(), options.compression_level(), options.window_log(),
             options.size_hint());
}

template <typename Dest>
template <typename... DestArgs>
inline BrotliWriter<Dest>::BrotliWriter(std::tuple<DestArgs...> dest_args,
                                        Options options)
    : BrotliWriterBase(std::move(options.dictionary()),
                       std::move(options.allocator()), options.buffer_size(),
                       options.size_hint()),
      dest_(std::move(dest_args)) {
  Initialize(dest_.get(), options.compression_level(), options.window_log(),
             options.size_hint());
}

template <typename Dest>
inline BrotliWriter<Dest>::BrotliWriter(BrotliWriter&& that) noexcept
    : BrotliWriterBase(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      dest_(std::move(that.dest_)) {}

template <typename Dest>
inline BrotliWriter<Dest>& BrotliWriter<Dest>::operator=(
    BrotliWriter&& that) noexcept {
  BrotliWriterBase::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  dest_ = std::move(that.dest_);
  return *this;
}

template <typename Dest>
inline void BrotliWriter<Dest>::Reset(Closed) {
  BrotliWriterBase::Reset(kClosed);
  dest_.Reset();
}

template <typename Dest>
inline void BrotliWriter<Dest>::Reset(const Dest& dest, Options options) {
  BrotliWriterBase::Reset(std::move(options.dictionary()),
                          std::move(options.allocator()), options.buffer_size(),
                          options.size_hint());
  dest_.Reset(dest);
  Initialize(dest_.get(), options.compression_level(), options.window_log(),
             options.size_hint());
}

template <typename Dest>
inline void BrotliWriter<Dest>::Reset(Dest&& dest, Options options) {
  BrotliWriterBase::Reset(std::move(options.dictionary()),
                          std::move(options.allocator()), options.buffer_size(),
                          options.size_hint());
  dest_.Reset(std::move(dest));
  Initialize(dest_.get(), options.compression_level(), options.window_log(),
             options.size_hint());
}

template <typename Dest>
template <typename... DestArgs>
inline void BrotliWriter<Dest>::Reset(std::tuple<DestArgs...> dest_args,
                                      Options options) {
  BrotliWriterBase::Reset(std::move(options.dictionary()),
                          std::move(options.allocator()), options.buffer_size(),
                          options.size_hint());
  dest_.Reset(std::move(dest_args));
  Initialize(dest_.get(), options.compression_level(), options.window_log(),
             options.size_hint());
}

template <typename Dest>
void BrotliWriter<Dest>::Done() {
  BrotliWriterBase::Done();
  if (dest_.is_owning()) {
    if (ABSL_PREDICT_FALSE(!dest_->Close())) Fail(*dest_);
  }
}

template <typename Dest>
bool BrotliWriter<Dest>::FlushImpl(FlushType flush_type) {
  if (ABSL_PREDICT_FALSE(!BrotliWriterBase::FlushImpl(flush_type))) {
    return false;
  }
  if (flush_type != FlushType::kFromObject || dest_.is_owning()) {
    if (ABSL_PREDICT_FALSE(!dest_->Flush(flush_type))) return Fail(*dest_);
  }
  return true;
}

}  // namespace riegeli

#endif  // RIEGELI_BROTLI_BROTLI_WRITER_H_
