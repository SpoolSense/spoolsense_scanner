#ifndef HTCW_STREAM_HPP
#define HTCW_STREAM_HPP
#include <htcw_endian.hpp>
#ifdef ARDUINO
#include <Arduino.h>
#ifdef SAMD_SERIES 
#ifndef IO_SEEED_FS
    #define IO_NO_FS
#endif
#endif
#ifdef ARDUINO_ARCH_STM32
    #define IO_NO_FS

#endif
#if defined(ARDUINO_ARDUINO_NANO33BLE) || defined(ARDUINO_ARCH_MBED_RP2040)|| defined(ARDUINO_ARCH_RP2040)
    #define IO_NO_FS
#endif
#ifndef IO_NO_FS
    #ifdef IO_ARDUINO_SD_FS
        #include <SD.h>
    #else
        #if !defined(SAMD_SERIES)
            #include <FS.h>
        #endif
    #endif
#endif
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP8266)
#include <pgmspace.h>
#elif defined(ARDUINO_ARDUINO_NANO33BLE) || defined(ARDUINO_ARCH_MBED_RP2040)|| defined(ARDUINO_ARCH_RP2040)
#include <api/deprecated-avr-comp/avr/pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif
#else
    #include <stdio.h>
    #include <string.h>
    #include <inttypes.h>
#endif
#if defined(SAMD_SERIES) && defined(IO_SEEED_FS)
    #include <Seeed_FS.h>
    #include "SD/Seeed_SD.h"
#endif
namespace io {
    struct stream_caps {
        uint8_t read : 1;
        uint8_t write : 1;
        uint8_t seek :1;
    };
    enum struct seek_origin {
        start = 0,
        current = 1,
        end = 2
    };
    class stream {
    public:
        virtual int getch()=0;
        virtual size_t read(uint8_t* destination,size_t size)=0;
        virtual int putch(int value)=0;
        virtual size_t write(const uint8_t* source,size_t size)=0;
        virtual unsigned long long seek(long long position,seek_origin origin=seek_origin::start)=0;
        virtual stream_caps caps() const=0;
        template<typename T>
        size_t read(T* out_value) {
            return read((uint8_t*)out_value,sizeof(T));
        }
        template<typename T>
        T read(const T& default_value=T()) {
            T result;
            if(sizeof(T)!=read(&result))
                return default_value;
            return result;
        }
        template<typename T>
        size_t write(const T& value) {
            return write((uint8_t*)&value,sizeof(T));
        }
    };
    enum struct file_mode {
        read = 1,
        write = 2,
        append = 6
    };
    class buffer_stream final : public stream {
        uint8_t* m_begin;
        uint8_t* m_current;
        size_t m_size;
        buffer_stream(const buffer_stream& rhs)=delete;
        buffer_stream& operator=(const buffer_stream& rhs)=delete;
    public:
        buffer_stream();
        buffer_stream(uint8_t* buffer,size_t size);
        buffer_stream(buffer_stream&& rhs);
        buffer_stream& operator=(buffer_stream&& rhs);
        uint8_t* handle();
        void set(uint8_t* buffer,size_t size);
        virtual int getch();
        virtual int putch(int value);
        virtual stream_caps caps() const;
        virtual size_t read(uint8_t* destination,size_t size);
        virtual size_t write(const uint8_t* source,size_t size);
        virtual unsigned long long seek(long long position,seek_origin origin=seek_origin::start);
    };
    class const_buffer_stream final : public stream {
        const uint8_t* m_begin;
        const uint8_t* m_current;
        size_t m_size;
        const_buffer_stream(const const_buffer_stream& rhs)=delete;
        const_buffer_stream& operator=(const const_buffer_stream& rhs)=delete;
    public:
        const_buffer_stream();
        const_buffer_stream(const uint8_t* buffer,size_t size);
        const_buffer_stream(const_buffer_stream&& rhs);
        const_buffer_stream& operator=(const_buffer_stream&& rhs);
        const uint8_t* handle() const;
        void set(const uint8_t* buffer,size_t size);
        virtual int getch();
        virtual int putch(int value);
        virtual stream_caps caps() const;
        virtual size_t read(uint8_t* destination,size_t size);
        virtual size_t write(const uint8_t* source,size_t size);
        virtual unsigned long long seek(long long position,seek_origin origin=seek_origin::start);
    };
#ifdef ARDUINO

    class arduino_stream final : public stream {
        Stream* m_stream;
        arduino_stream(const arduino_stream& rhs) = delete;
        arduino_stream& operator=(const arduino_stream& rhs)=delete;
    public:
        arduino_stream();
        arduino_stream(Stream* stream);
        arduino_stream(arduino_stream&& rhs);
        arduino_stream& operator=(arduino_stream&& rhs);
        Stream* handle();
        void set(Stream* stream);
        virtual size_t read(uint8_t* destination,size_t size);
        virtual int getch();
        virtual size_t write(const uint8_t* source,size_t size);
        virtual int putch(int value);
        virtual unsigned long long seek(long long position, seek_origin origin=seek_origin::start);
        virtual stream_caps caps() const;
    };
#endif
#ifndef IO_NO_FS
    class file_stream final : public stream {
#ifdef ARDUINO
        File* m_file;
#else
        FILE* m_fd;
#endif
        stream_caps m_caps;
        file_stream(const file_stream& rhs) = delete;
        file_stream& operator=(const file_stream& rhs)=delete;
    public:
        file_stream();
#ifdef ARDUINO
        file_stream(File& file);
        file_stream(file_stream&& rhs);
        file_stream& operator=(file_stream&& rhs);
        ~file_stream();
        File* handle() const;
        void set(File& file);
        virtual size_t read(uint8_t* destination,size_t size);
        virtual int getch();
        virtual size_t write(const uint8_t* source,size_t size);
        virtual int putch(int value);
        virtual unsigned long long seek(long long position, seek_origin origin=seek_origin::start);
        void close();
#else
        file_stream(const char* name,file_mode mode=file_mode::read);
        file_stream(file_stream&& rhs);
        file_stream& operator=(file_stream&& rhs);
        ~file_stream();
        FILE* handle() const;
        void set(const char* name,file_mode mode = file_mode::read);
        virtual size_t read(uint8_t* destination,size_t size);
        virtual int getch();
        virtual size_t write(const uint8_t* source,size_t size);
        virtual int putch(int value);
        virtual unsigned long long seek(long long position, seek_origin origin=seek_origin::start);
        void close();
#endif
        virtual stream_caps caps() const;
    };

#ifdef ARDUINO
    class c_file_stream final : public stream {
        FILE* m_fd;
        io::stream_caps m_caps;
        c_file_stream(const c_file_stream& rhs) = delete;
        c_file_stream& operator=(const c_file_stream& rhs)=delete;
    public:
        c_file_stream();
        c_file_stream(const char* name,file_mode mode=file_mode::read);
        c_file_stream(c_file_stream&& rhs);
        c_file_stream& operator=(c_file_stream&& rhs);
        ~c_file_stream();
        FILE* handle() const;
        void set(const char* name,file_mode mode = file_mode::read);
        virtual size_t read(uint8_t* destination,size_t size);
        virtual int getch();
        virtual size_t write(const uint8_t* source,size_t size);
        virtual int putch(int value);
        virtual unsigned long long seek(long long position, seek_origin origin=seek_origin::start);
        void close();
        virtual io::stream_caps caps() const;
    };
#else
    using c_file_stream = file_stream;
#endif

#endif
    class stream_reader_base {
    protected:
        stream* m_stream;
        stream_reader_base(io::stream* stream) : m_stream(stream) {
        }
    public:
        inline bool initialized() const {
            return m_stream!=nullptr;
        }
        inline stream* base_stream() const {
            return m_stream;
        }
    };
    class stream_reader_le : public stream_reader_base {
     public:
        stream_reader_le(io::stream* stream);
        bool read(uint8_t* out_value) const;
        bool read(uint16_t* out_value) const;
        bool read(uint32_t* out_value) const;
#if HTCW_MAX_WORD >= 64
        bool read(uint64_t* out_value) const;
#endif
        bool read(int8_t* out_value) const;
        bool read(int16_t* out_value) const;
        bool read(int32_t* out_value) const;
#if HTCW_MAX_WORD >= 64
        bool read(int64_t* out_value) const;
#endif
    };
    class stream_reader_be : public stream_reader_base {
    public:
        stream_reader_be(io::stream* stream);
        bool read(uint8_t* out_value) const;
        bool read(uint16_t* out_value) const;
        bool read(uint32_t* out_value) const;
#if HTCW_MAX_WORD >= 64
        bool read(uint64_t* out_value) const;
#endif
        bool read(int8_t* out_value) const;
        bool read(int16_t* out_value) const;
        bool read(int32_t* out_value) const;
#if HTCW_MAX_WORD >= 64
        bool read(int64_t* out_value) const;
#endif
    };
}
#endif