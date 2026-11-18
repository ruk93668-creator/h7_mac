# lwIP upstream source

- Stable release: **2.2.1**, `STABLE-2_2_1_RELEASE`, announced on the [official lwIP list](https://lists.gnu.org/archive/html/lwip-users/2025-02/msg00001.html).
- Upstream mirror: [lwip-tcpip/lwip](https://github.com/lwip-tcpip/lwip); annotated tag object `009c2256469004009488b3385ba269461e8eb616`; peeled [commit](https://github.com/lwip-tcpip/lwip/commit/77dcd25a72509eb83f72b033d219b1d40cd8eb95) `77dcd25a72509eb83f72b033d219b1d40cd8eb95`.
- Imported [tag archive](https://codeload.github.com/lwip-tcpip/lwip/tar.gz/refs/tags/STABLE-2_2_1_RELEASE) SHA-256: `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539`. The digest is for this specific tar.gz archive, not a release ZIP or the Git commit.
- The original [COPYING](COPYING), README, CHANGELOG, upstream headers and the 17 compiled C files are preserved byte for byte. The exact compiled source list is in `Tests/Ethernet/e4_sources.py`. Unused upstream protocol headers are retained for upstream internal includes; TCP/UDP/DHCP and application implementations are not built.
- Local port and configuration files are outside this directory, under `Platform/Ethernet`. No edits were made to imported sources.
