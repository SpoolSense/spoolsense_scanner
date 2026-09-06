#ifndef HTCW_LEX_SOURCE_HPP
#define HTCW_LEX_SOURCE_HPP
#include "io_stream.hpp"
namespace io {
    template<size_t Capacity> class lex_source {
        static_assert(Capacity>4,"The capacity must be at least 5.");
        int m_state;
        int32_t m_current;
        unsigned long long m_position;
        char m_capture[Capacity];
        size_t m_capture_size;
        bool (*m_capture_flush)(const char*,size_t,void*);
        void* m_capture_flush_state;
        stream* m_stream;
    public:
        static const int8_t io_error = -3;
        static const int8_t out_of_memory_error=-4;
    private:
        lex_source(lex_source& rhs) = delete;
        lex_source& operator=(lex_source& rhs) = delete;
        
        const int8_t initial = -5;
        bool advance_impl() {
            int16_t ch;
            int16_t i = read();
            if(0>i) {
                m_state = i;
                m_current = 0;
                return false;
            }
            m_current = i;
            m_state = 0;
            
            ++m_position;
            if(m_current<128) {    
                return -1<m_state;
            }
            
            if (0xf0 == (0xf8 & m_current)) {
                // 4 byte utf8 codepoint
                m_current = (0x07 & m_current) << 18;
                ch = read();
                if(end_of_input==ch||closed==ch||io_error==ch) {
                    m_state = io_error;
                    return false;
                }
                m_current |= (0x3f & ch) << 12;
                ch = read();
                if(end_of_input==ch||closed==ch||io_error==ch) {
                    m_state = io_error;
                    return false;
                }
                m_current |= (0x3f & ch) << 6;
                ch = read();
                if(end_of_input==ch||closed==ch||io_error==ch) {
                    m_state = io_error;
                    return false;
                }
                m_current |= 0x3f & ch;
                ++m_position;
                return true;
            }
            if (0xe0 == (0xf0 & m_current)) {
                // 3 byte utf8 codepoint
                m_current = (0x0f & m_current) << 12;
                ch = read();
                if(end_of_input==ch||closed==ch||io_error==ch) {
                    m_state = io_error;
                    return false;
                }
                m_current |= (0x3f & ch) << 6;
                ch = read();
                if(end_of_input==ch||closed==ch||io_error==ch) {
                    m_state = io_error;
                    return false;
                }
                m_current |= 0x3f & ch;
                ++m_position;
                return true;
            }
            // 2 byte utf8 codepoint
            m_current = (0x1f & m_current) << 6;
            ch = read();
            if(end_of_input==ch||closed==ch||io_error==ch) {
                m_state = io_error;
                return false;
            }
            m_current |= 0x3f & ch;
            ++m_position;
            return true;
        }
        static const int8_t end_of_input=-1;
        static const int8_t closed=-2;
        inline int read() {
            if(nullptr==m_stream) {
                return closed;
            }
            int result = m_stream->getch();
            if(-1==result)
                return end_of_input;
            return result;
        }
        
        bool skip_to_any(const char* characters7bit,unsigned long long& position,int16_t& match,int8_t& error) {
            error = 0;
            const char *sz;
            if(current()>-1&&current()<128) {
                match=(int16_t)current();
                sz = strchr(characters7bit,match);
                if(nullptr!=sz) {
                    match=*sz;
                    return true;
                }
            }
            while(-1<(match=read())) {
                ++position;
                if(match>127)
                    continue;
                sz = strchr(characters7bit,(char)match);
                if(nullptr!=sz) {
                    match=*sz;
                    return true;
                }
            
            }
            error = match;
            return false;
        }
        bool append_capture(char ch) {
             char*sz = m_capture+(m_capture_size++);
            *(sz++)=ch;
            *sz=0;
            return true;
        }
        void clear_error() {
            m_state = 0;
        }
        
    public:
        inline lex_source(stream* input) : m_capture_flush(nullptr) {
            m_stream = input;
            reset();
        }
        lex_source(lex_source&& rhs) :
            m_state(rhs.m_state),
            m_current(rhs.m_current),
            m_position(rhs.m_position),
            m_capture_size(rhs.m_capture_size),
            m_capture_flush(rhs.m_capture_flush),
            m_capture_flush_state(rhs.m_capture_flush_state),
            m_stream(rhs.m_stream) {
            memcpy(m_capture,rhs.m_capture,rhs.m_capture_size+1);
            rhs.m_stream = nullptr;
        }
        lex_source& operator=(lex_source&& rhs) {
            m_state=rhs.m_state;
            m_current=rhs.m_current;
            m_position=rhs.m_position;
            memcpy(m_capture,rhs.m_capture,rhs.m_capture_size+1);
            m_capture_size=rhs.m_capture_size;
            m_capture_flush = rhs.m_capture_flush;
            m_capture_flush_state = rhs.m_capture_flush_state;
            m_stream=rhs.m_stream;
            rhs.m_stream = nullptr;
            return *this;
        }
        inline static bool is_whitespace(uint32_t codepoint) {
            return ((codepoint>=9 && codepoint<=13) ||
                codepoint==32 ||
                codepoint==133 ||
                codepoint==160 ||
                codepoint==5760 ||
                (codepoint>=8192 && codepoint<=8202) ||
                codepoint==8232 ||
                codepoint==8233 ||
                codepoint==8239 ||
                codepoint==8287 ||
                codepoint==1288);
        }
        inline char* capture_buffer() {
            return m_capture;
        }
        inline const char* const_capture_buffer() const {
            return m_capture;
        }
        inline size_t capture_capacity()  const {return Capacity - 1;}
        inline size_t capture_size() const {return m_capture_size;}
        bool capture_fill(char* utf8) {
            char*sz = capture_buffer();
            while(0!=*sz) ++sz;
            size_t free_len = capture_capacity()-capture_size();
            size_t len = strlen(utf8);
            if(free_len<=len) return false;
            memcpy(sz,utf8,len+1);
            m_capture_size+=len;
            return true;
        }
        void clear_capture() {
            m_capture_size=0;
            *m_capture=0;
            // clear out of memory errors
            if(out_of_memory_error==error())
                clear_error();
        }
        bool flush_capture() {
            if(m_capture_flush!=nullptr) {
                if(!m_capture_flush(m_capture,m_capture_size,m_capture_flush_state)) {
                    m_current = io_error;
                    return false;
                }
                clear_capture();
                return true;
            }
            return false;
        }
        void set_flush_capture(bool(*callback)(const char*,size_t,void*),void* state) {
            flush_capture();
            m_capture_flush= callback;
            m_capture_flush_state = state;
        }
        void reset() {
            m_state = initial;
            m_position = 0;
            m_current = 0; 
            m_capture_size=0;
            *m_capture=0;
        }
        bool skip(bool(*fn)(uint32_t)) {
            if(!ensure_started()) return false;
            bool result = false;
            while(more() && fn(m_current)) {
                advance();
                result = true;
            }
            return result;
        }
        bool read(bool(*fn)(uint32_t)) {
            if(!ensure_started()) return false;
            bool result = false;
            while(more() && fn(m_current)) {
                if(!capture(m_current)) {
                    return false;
                }
                advance();
                result = true;
            }
            return result;
        }
        
        bool read_until_full(bool(*fn)(uint32_t)) {
            if(!ensure_started()) return false;
            bool result = false;
            while(more() && fn(m_current) && capture_capacity()-capture_size()>=4) {
                if(!capture(m_current)) {
                    return false;
                }
                advance();
                result = true;
            }
            return result;
        }
        char skip_to_any(const char* characters7bit) {
            int16_t match;
            unsigned long long int pos=m_position;
            int8_t error=0;
            if(!skip_to_any(characters7bit,pos,match,error)) {
                if(0>error)
                    m_state=error;
                m_position=pos;
                return false;
            }
            m_current = match;
            //printf("Advanced %d characters\r\n",(int)(pos-m_position));
            m_position=pos;
            return match;
        }

        inline bool advance() {
            return advance_impl();
        }
        bool advance(bool capture) {
            if (end_of_input == m_state || io_error==m_state) {
                m_current = 0;
                return false;
            }

            if(capture) {
                if(capture_size()>capture_capacity()-4) {
                    if(!flush_capture()) {
                        m_state=out_of_memory_error;
                        m_current=0;
                    }
                    return false;
                }
                return advance_impl() && this->capture(m_current);
            }
            return advance_impl();
        }
        inline int32_t current() const {
            return m_current;
        }
        bool ensure_started() {
            if(m_state==initial) {
                return advance_impl();
            }
            return true;
        }
        inline unsigned long long position() const { return m_position; }
        inline bool more() const {
            return -1<m_state||initial==m_state;
        }
        inline bool eof() const {return end_of_input==m_state;};
        inline bool has_error() const {return -1> m_state;};
        inline int8_t error() const {if(has_error()) return m_state; return 0;}
        bool capture(int32_t codepoint) {
            size_t n = capture_capacity()-capture_size();
            if(n<4) {
                if(!flush_capture()) {
                    m_state = out_of_memory_error; 
                    return false;
                }
            }
            if (0 == ((int32_t)0xffffff80 & codepoint)) {
                // 1-byte/7-bit ascii
                // (0b0xxxxxxx)
                append_capture(codepoint);
                return true;
            }
            if (0 == ((int32_t)0xfffff800 & codepoint)) {
                // 2-byte/11-bit utf8 code point
                // (0b110xxxxx 0b10xxxxxx)
                append_capture(0xc0 | (char)(codepoint >> 6));                
                append_capture(0x80 | (char)(codepoint & 0x3f));
                return true;
            }
            if (0 == ((int32_t)0xffff0000 & codepoint)) {
                // 3-byte/16-bit utf8 code point
                append_capture(0xe0 | (char)(codepoint >> 12));
                append_capture(0x80 | (char)((codepoint >> 6) & 0x3f));
                append_capture(0x80 | (char)(codepoint & 0x3f));
                return true;
            } 
            // 4-byte/21-bit utf8 code point
            // (0b11110xxx 0b10xxxxxx 0b10xxxxxx 0b10xxxxxx)
            append_capture(0xf0 | (char)(codepoint >> 18));
            append_capture(0x80 | (char)((codepoint >> 12) & 0x3f));
            append_capture(0x80 | (char)((codepoint >> 6) & 0x3f));
            append_capture(0x80 | (char)(codepoint & 0x3f));
            return true;
        }
        
    };
}
#endif