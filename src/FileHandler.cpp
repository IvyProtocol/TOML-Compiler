#include "FileHandler.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>


namespace FileHandler
{

  FileWriter::FileWriter(const char* FilePath) noexcept
  {
    this->FileDesc = ::open(FilePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }

  FileWriter::~FileWriter() noexcept
  {

    if
    (
      this->FileDesc != -1
    ) ::close(this->FileDesc);
  }

  bool FileWriter::IsOpen() const noexcept
  {
    return this->FileDesc != -1;
  }

  bool FileWriter::Write(std::string_view data) noexcept
  {

    if
    (
      !IsOpen()
    ) return false;

    ssize_t BytesWritten = ::write(
      this->FileDesc,
      data.data(),
      data.size()
    );

    return BytesWritten == static_cast<ssize_t>(data.size());

  }

  constexpr auto FileOpener::load
  (
    std::filesystem::path const& FilePath_
  ) noexcept(true) -> bool
  {

    this->release();
    std::error_code errCode {};

    auto const FileSize
    {
      std::filesystem::file_size
      (
        std::move(FilePath_),
        errCode
      )
    };

    if
    (
      errCode ||
      FileSize == 0
    ) return false;

    auto const FileDesc
    {
      ::open
      (
        FilePath_.c_str(),
        O_RDONLY
      )
    };

    if
    (
      FileDesc == -1
    ) return false;

    this->sourceView = {
      reinterpret_cast<char const*>
      (
        ::mmap
        (
          nullptr,
          FileSize,
          PROT_READ,
          MAP_PRIVATE,
          FileDesc,
          0ZU
        )
      ),
      FileSize
    };

    ::close(FileDesc);

    if
    (
      this->sourceView.data() == MAP_FAILED
    ) {

      this->sourceView = {};
      return false;

    }

    ::madvise
    (
      const_cast<void* const>
      (
        static_cast<void const* const>(this->sourceView.data())
      ),
      FileSize,
      MADV_SEQUENTIAL
    );
    return true;
  }

  constexpr auto FileOpener::release(void) noexcept(true) -> void
  {

    if
    (
      !this->empty() &&
      this->data() != MAP_FAILED
    ) {

      ::munmap
      (
        const_cast<void*>
        (
          static_cast<void const* const>
          (
            this->data()
          )
        ),
        this->size()
      );

      this->sourceView = {};

    }

  }


  auto FileOpener::operator = (FileOpener&& other) noexcept(true) -> FileOpener&
  {

    if
    (
      this != &other
    ) {

      this->release();
      sourceView = std::move(other.sourceView);
      other.sourceView = {};

    }

    return *this;

  }

  FileOpener::FileOpener(std::filesystem::path const& FilePath_) noexcept(true)
  {

    if
    (
      this->load(FilePath_)
    ) return;

    else [[
      /* nullAttr*/
    ]] this->release();

  }

  FileOpener::~FileOpener(void) noexcept(true) { this->release(); }

}
