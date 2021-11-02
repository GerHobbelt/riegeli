// Copyright 2018 Google LLC
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

#ifndef RIEGELI_ZLIB_ZLIB_WRITER_H_
#define RIEGELI_ZLIB_ZLIB_WRITER_H_

#include <stddef.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "riegeli/base/base.h"
#include "riegeli/base/dependency.h"
#include "riegeli/base/object.h"
#include "riegeli/base/recycling_pool.h"
#include "riegeli/bytes/buffered_writer.h"
#include "riegeli/bytes/writer.h"
#include "riegeli/zlib/zlib_dictionary.h"
#include "zconf.h"
#include "zlib.h"

namespace riegeli {

// Template parameter independent part of `ZlibWriter`.
class ZlibWriterBase : public BufferedWriter {
 public:
  // Specifies what format of header to write.
  enum class Header {
    // Zlib header.
    kZlib = 0,
    // Gzip header.
    kGzip = 16,
    // No header; decompressor must expect no header too.
    kRaw = -1,
  };

  class Options {
   public:
    Options() noexcept {}

    // Tunes the tradeoff between compression density and compression speed
    // (higher = better density but slower).
    //
    // `compression_level` must be between `kMinCompressionLevel` (0) and
    // `kMaxCompressionLevel` (9). Default: `kDefaultCompressionLevel` (6).
    static constexpr int kMinCompressionLevel = Z_NO_COMPRESSION;
    static constexpr int kMaxCompressionLevel = Z_BEST_COMPRESSION;
    static constexpr int kDefaultCompressionLevel = 6;
    Options& set_compression_level(int compression_level) & {
      RIEGELI_ASSERT_GE(compression_level, kMinCompressionLevel)
          << "Failed precondition of "
             "ZlibWriterBase::Options::set_compression_level(): "
             "compression level out of range";
      RIEGELI_ASSERT_LE(compression_level, kMaxCompressionLevel)
          << "Failed precondition of "
             "ZlibWriterBase::Options::set_compression_level()"
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
    // `window_log` must be between `kMinWindowLog` (9) and
    // `kMaxWindowLog` (15). Default: `kDefaultWindowLog` (15).
    static constexpr int kMinWindowLog = 9;
    static constexpr int kMaxWindowLog = MAX_WBITS;
    static constexpr int kDefaultWindowLog = MAX_WBITS;
    Options& set_window_log(int window_log) & {
      RIEGELI_ASSERT_GE(window_log, kMinWindowLog)
          << "Failed precondition of "
             "ZlibWriterBase::Options::set_window_log(): "
             "window log out of range";
      RIEGELI_ASSERT_LE(window_log, kMaxWindowLog)
          << "Failed precondition of "
             "ZlibWriterBase::Options::set_window_log(): "
             "window log out of range";
      window_log_ = window_log;
      return *this;
    }
    Options&& set_window_log(int window_log) && {
      return std::move(set_window_log(window_log));
    }
    int window_log() const { return window_log_; }

    // What format of header to write.
    //
    // Default: `Header::kZlib`.
    static constexpr Header kDefaultHeader = Header::kZlib;
    Options& set_header(Header header) & {
      header_ = header;
      return *this;
    }
    Options&& set_header(Header header) && {
      return std::move(set_header(header));
    }
    Header header() const { return header_; }

    // Zlib dictionary. The same dictionary must be used for decompression.
    //
    // Default: `ZlibDictionary()`.
    Options& set_dictionary(const ZlibDictionary& dictionary) & {
      dictionary_ = dictionary;
      return *this;
    }
    Options& set_dictionary(ZlibDictionary&& dictionary) & {
      dictionary_ = std::move(dictionary);
      return *this;
    }
    Options&& set_dictionary(const ZlibDictionary& dictionary) && {
      return std::move(set_dictionary(dictionary));
    }
    Options&& set_dictionary(ZlibDictionary&& dictionary) && {
      return std::move(set_dictionary(std::move(dictionary)));
    }
    ZlibDictionary& dictionary() { return dictionary_; }
    const ZlibDictionary& dictionary() const { return dictionary_; }

    // Expected uncompressed size, or `absl::nullopt` if unknown. This may
    // improve performance.
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
             "ZlibWriterBase::Options::set_buffer_size(): "
             "zero buffer size";
      buffer_size_ = buffer_size;
      return *this;
    }
    Options&& set_buffer_size(size_t buffer_size) && {
      return std::move(set_buffer_size(buffer_size));
    }
    size_t buffer_size() { return buffer_size_; }

   private:
    int compression_level_ = kDefaultCompressionLevel;
    int window_log_ = kDefaultWindowLog;
    Header header_ = kDefaultHeader;
    ZlibDictionary dictionary_;
    absl::optional<Position> size_hint_;
    size_t buffer_size_ = kDefaultBufferSize;
  };

  // Returns the compressed `Writer`. Unchanged by `Close()`.
  virtual Writer* dest_writer() = 0;
  virtual const Writer* dest_writer() const = 0;

 protected:
  explicit ZlibWriterBase(Closed) noexcept : BufferedWriter(kClosed) {}

  explicit ZlibWriterBase(ZlibDictionary&& dictionary, size_t buffer_size,
                          absl::optional<Position> size_hint);

  ZlibWriterBase(ZlibWriterBase&& that) noexcept;
  ZlibWriterBase& operator=(ZlibWriterBase&& that) noexcept;

  void Reset(Closed);
  void Reset(ZlibDictionary&& dictionary, size_t buffer_size,
             absl::optional<Position> size_hint);
  static int GetWindowBits(const Options& options);
  void Initialize(Writer* dest, int compression_level, int window_bits);

  void DoneBehindBuffer(absl::string_view src) override;
  void Done() override;
  // `ZlibWriterBase` overrides `Writer::DefaultAnnotateStatus()` to annotate
  // the status with the current position, clarifying that this is the
  // uncompressed position. A status propagated from `*dest_writer()` might
  // carry annotation with the compressed position.
  ABSL_ATTRIBUTE_COLD void DefaultAnnotateStatus() override;
  bool WriteInternal(absl::string_view src) override;
  bool FlushBehindBuffer(absl::string_view src, FlushType flush_type);

 private:
  struct ZStreamDeleter {
    void operator()(z_stream* ptr) const {
      const int result = deflateEnd(ptr);
      RIEGELI_ASSERT(result == Z_OK || result == Z_DATA_ERROR)
          << "deflateEnd() failed: " << result;
      delete ptr;
    }
  };
  struct ZStreamKey {
    friend bool operator==(ZStreamKey a, ZStreamKey b) {
      return a.compression_level == b.compression_level &&
             a.window_bits == b.window_bits;
    }
    friend bool operator!=(ZStreamKey a, ZStreamKey b) {
      return a.compression_level != b.compression_level ||
             a.window_bits != b.window_bits;
    }
    template <typename HashState>
    friend HashState AbslHashValue(HashState hash_state, ZStreamKey self) {
      return HashState::combine(std::move(hash_state), self.compression_level,
                                self.window_bits);
    }

    int compression_level;
    int window_bits;
  };

  ABSL_ATTRIBUTE_COLD bool FailOperation(absl::string_view operation,
                                         int zlib_code);
  bool WriteInternal(absl::string_view src, Writer& dest, int flush);

  ZlibDictionary dictionary_;
  KeyedRecyclingPool<z_stream, ZStreamKey, ZStreamDeleter>::Handle compressor_;
};

// A `Writer` which compresses data with Zlib before passing it to another
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
// The compressed `Writer` must not be accessed until the `ZlibWriter` is closed
// or no longer used, except that it is allowed to read the destination of the
// compressed `Writer` immediately after `Flush()`.
template <typename Dest = Writer*>
class ZlibWriter : public ZlibWriterBase {
 public:
  // Creates a closed `ZlibWriter`.
  explicit ZlibWriter(Closed) noexcept : ZlibWriterBase(kClosed) {}

  ABSL_DEPRECATED("Use kClosed constructor instead")
  ZlibWriter() noexcept : ZlibWriter(kClosed) {}

  // Will write to the compressed `Writer` provided by `dest`.
  explicit ZlibWriter(const Dest& dest, Options options = Options());
  explicit ZlibWriter(Dest&& dest, Options options = Options());

  // Will write to the compressed `Writer` provided by a `Dest` constructed from
  // elements of `dest_args`. This avoids constructing a temporary `Dest` and
  // moving from it.
  template <typename... DestArgs>
  explicit ZlibWriter(std::tuple<DestArgs...> dest_args,
                      Options options = Options());

  ZlibWriter(ZlibWriter&& that) noexcept;
  ZlibWriter& operator=(ZlibWriter&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `ZlibWriter`. This avoids
  // constructing a temporary `ZlibWriter` and moving from it.
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
explicit ZlibWriter(Closed)->ZlibWriter<DeleteCtad<Closed>>;
template <typename Dest>
explicit ZlibWriter(const Dest& dest,
                    ZlibWriterBase::Options options = ZlibWriterBase::Options())
    -> ZlibWriter<std::decay_t<Dest>>;
template <typename Dest>
explicit ZlibWriter(Dest&& dest,
                    ZlibWriterBase::Options options = ZlibWriterBase::Options())
    -> ZlibWriter<std::decay_t<Dest>>;
template <typename... DestArgs>
explicit ZlibWriter(std::tuple<DestArgs...> dest_args,
                    ZlibWriterBase::Options options = ZlibWriterBase::Options())
    -> ZlibWriter<DeleteCtad<std::tuple<DestArgs...>>>;
#endif

// Implementation details follow.

inline ZlibWriterBase::ZlibWriterBase(ZlibDictionary&& dictionary,
                                      size_t buffer_size,
                                      absl::optional<Position> size_hint)
    : BufferedWriter(buffer_size, size_hint),
      dictionary_(std::move(dictionary)) {}

inline ZlibWriterBase::ZlibWriterBase(ZlibWriterBase&& that) noexcept
    : BufferedWriter(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      dictionary_(std::move(that.dictionary_)),
      compressor_(std::move(that.compressor_)) {}

inline ZlibWriterBase& ZlibWriterBase::operator=(
    ZlibWriterBase&& that) noexcept {
  BufferedWriter::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  dictionary_ = std::move(that.dictionary_);
  compressor_ = std::move(that.compressor_);
  return *this;
}

inline void ZlibWriterBase::Reset(Closed) {
  BufferedWriter::Reset(kClosed);
  compressor_.reset();
  dictionary_ = ZlibDictionary();
}

inline void ZlibWriterBase::Reset(ZlibDictionary&& dictionary,
                                  size_t buffer_size,
                                  absl::optional<Position> size_hint) {
  BufferedWriter::Reset(buffer_size, size_hint);
  compressor_.reset();
  dictionary_ = std::move(dictionary);
}

inline int ZlibWriterBase::GetWindowBits(const Options& options) {
  return options.header() == Header::kRaw
             ? -options.window_log()
             : options.window_log() + static_cast<int>(options.header());
}

template <typename Dest>
inline ZlibWriter<Dest>::ZlibWriter(const Dest& dest, Options options)
    : ZlibWriterBase(std::move(options.dictionary()), options.buffer_size(),
                     options.size_hint()),
      dest_(dest) {
  Initialize(dest_.get(), options.compression_level(), GetWindowBits(options));
}

template <typename Dest>
inline ZlibWriter<Dest>::ZlibWriter(Dest&& dest, Options options)
    : ZlibWriterBase(std::move(options.dictionary()), options.buffer_size(),
                     options.size_hint()),
      dest_(std::move(dest)) {
  Initialize(dest_.get(), options.compression_level(), GetWindowBits(options));
}

template <typename Dest>
template <typename... DestArgs>
inline ZlibWriter<Dest>::ZlibWriter(std::tuple<DestArgs...> dest_args,
                                    Options options)
    : ZlibWriterBase(std::move(options.dictionary()), options.buffer_size(),
                     options.size_hint()),
      dest_(std::move(dest_args)) {
  Initialize(dest_.get(), options.compression_level(), GetWindowBits(options));
}

template <typename Dest>
inline ZlibWriter<Dest>::ZlibWriter(ZlibWriter&& that) noexcept
    : ZlibWriterBase(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      dest_(std::move(that.dest_)) {}

template <typename Dest>
inline ZlibWriter<Dest>& ZlibWriter<Dest>::operator=(
    ZlibWriter&& that) noexcept {
  ZlibWriterBase::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  dest_ = std::move(that.dest_);
  return *this;
}

template <typename Dest>
inline void ZlibWriter<Dest>::Reset(Closed) {
  ZlibWriterBase::Reset(kClosed);
  dest_.Reset();
}

template <typename Dest>
inline void ZlibWriter<Dest>::Reset(const Dest& dest, Options options) {
  ZlibWriterBase::Reset(std::move(options.dictionary()), options.buffer_size(),
                        options.size_hint());
  dest_.Reset(dest);
  Initialize(dest_.get(), options.compression_level(), GetWindowBits(options));
}

template <typename Dest>
inline void ZlibWriter<Dest>::Reset(Dest&& dest, Options options) {
  ZlibWriterBase::Reset(std::move(options.dictionary()), options.buffer_size(),
                        options.size_hint());
  dest_.Reset(std::move(dest));
  Initialize(dest_.get(), options.compression_level(), GetWindowBits(options));
}

template <typename Dest>
template <typename... DestArgs>
inline void ZlibWriter<Dest>::Reset(std::tuple<DestArgs...> dest_args,
                                    Options options) {
  ZlibWriterBase::Reset(std::move(options.dictionary()), options.buffer_size(),
                        options.size_hint());
  dest_.Reset(std::move(dest_args));
  Initialize(dest_.get(), options.compression_level(), GetWindowBits(options));
}

template <typename Dest>
void ZlibWriter<Dest>::Done() {
  ZlibWriterBase::Done();
  if (dest_.is_owning()) {
    if (ABSL_PREDICT_FALSE(!dest_->Close())) Fail(*dest_);
  }
}

template <typename Dest>
bool ZlibWriter<Dest>::FlushImpl(FlushType flush_type) {
  if (ABSL_PREDICT_FALSE(!ZlibWriterBase::FlushImpl(flush_type))) return false;
  if (flush_type != FlushType::kFromObject || dest_.is_owning()) {
    if (ABSL_PREDICT_FALSE(!dest_->Flush(flush_type))) return Fail(*dest_);
  }
  return true;
}

}  // namespace riegeli

#endif  // RIEGELI_ZLIB_ZLIB_WRITER_H_
