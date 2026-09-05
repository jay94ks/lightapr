#ifndef APR_EMBEDDED_ASSETS_HPP
#define APR_EMBEDDED_ASSETS_HPP

#include <cstddef>

// Declarations for the byte arrays generated at build time by
// cmake/EmbedFile.cmake (see CMakeLists.txt's apr_embed_html calls) from
// monitor/html/index.html and monitor/tester/index.html. Definitions live in
// the generated .cpp files under <build>/generated/.
namespace apr {
namespace embedded {

extern const unsigned char monitor_html_data[];
extern const std::size_t monitor_html_size;

extern const unsigned char tester_html_data[];
extern const std::size_t tester_html_size;

} // namespace embedded
} // namespace apr

#endif // APR_EMBEDDED_ASSETS_HPP
