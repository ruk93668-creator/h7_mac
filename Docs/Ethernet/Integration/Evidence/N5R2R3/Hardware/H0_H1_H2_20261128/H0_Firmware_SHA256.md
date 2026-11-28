# H0 固件 SHA-256 核验

**PASS（软件）**。原始构建产物全部存在。360 个构建输入的 SHA-256 与交付清单一致。
候选构建提交为 5768c9f9afaf248cdcf4e1d155dce11c69a71b2c，远端联合分支为 e384e31e7836211f109db13cf52f1de7da64c8b7。

| 文件 | SHA-256（实测） |
| --- | --- |
| h7_network_acceptance.axf | cf3480182c79f9154ec162f13396a414df4ea437f9cb859244c50ad30f1aa88f |
| h7_network_acceptance.hex | a5201251f69132b6ac06880cae042e867cb05dbca1126593e4e3d6dc811481e0 |
| h7_network_acceptance.map | 0bd7de772b37c18fdb40eec6f7a21d71bb20bf037216f51f67a42a2032817103 |
| h7_mac_ethernet_acceptance.uvprojx | 559637d14c1149ca745bb0bfb108b9ed29caba2278c71fcf7515a1f5481a5bbb |
| service_tasks_acceptance.c | ec055851424f3c4c1a304d782bdc53e8c6eb62c1a6f9329634bedc84144b5b9f |
| network_board_acceptance.c | f187039efe7955719e1795c4b5a75893bc4e0a1fd8aa1e81fb83711a8d28a432 |
| network_board_acceptance.h | 4b0996e7b340fdb7094376dd3a8a6cd7163342d9e0eff28d23760b76f2f5bb6e |

Intel HEX 全部记录校验通过。217,992 字节覆盖 0x08000000–0x08035387，逐地址与 AXF Flash 装载数据相同。HEX 写入范围仅覆盖本次 CM7 镜像所在的 Flash 区间。已复制 AXF、HEX、MAP 到 firmware，并再次核对归档副本 SHA-256。

PC 识别到 CMSIS-DAP。以太网 3 为 192.168.137.1/24，链路 Up、100 Mbps，Windows 共享服务正在运行。V1 板标识、供电及网线连接待操作者现场核对。

[完整 SHA-256 原始结果](H0_Firmware_SHA256.json) · [构建清单](hardware_acceptance_manifest_reference.json)
