"""Generate the isolated CM7 V3 project beside the original Keil project."""

from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "MDK-ARM/h7_mac.uvprojx"
DESTINATION = PROJECT.with_name("h7_mac_application_v3.uvprojx")
BASE_TARGET = "h7_mac_CM7"
V3_TARGET = "h7_mac_CM7_V3"
SHARED_APPLICATION = {
    "app_events.c", "app_messages.c", "app_tasks.c", "app_status.c",
    "app_camera_lifecycle.c", "video_config.c", "ipc_task.c",
}
EXPECTED_V3 = {
    "Core/app_main.c", "Core/app_services.c",
    "Services/Camera/camera_service.c", "Services/USB/usb_service.c",
    "Services/Storage/storage_service.c", "Tasks/service_tasks.c",
}


def generate():
    tree = ET.parse(PROJECT)
    project = tree.getroot()
    targets = project.find("Targets")
    selected = [target for target in targets if target.findtext("TargetName") == BASE_TARGET]
    if len(selected) != 1:
        raise ValueError("Expected exactly one original CM7 Target")
    target = selected[0]
    for other in list(targets):
        if other is not target:
            targets.remove(other)

    target.find("TargetName").text = V3_TARGET
    common = target.find("TargetOption/TargetCommonOption")
    common.find("OutputDirectory").text = ".\\h7_mac_CM7_V3\\"
    common.find("OutputName").text = V3_TARGET
    defines = target.find("TargetOption/TargetArmAds/Cads/VariousControls/Define")
    defines.text += ",APP_V3_BUILD=1"

    # Keep only the pre-existing task, IPC and transport infrastructure used by V3.
    groups = target.find("Groups")
    retained = set()
    for group in groups:
        entries = group.find("Files")
        if entries is None:
            continue
        for entry in list(entries):
            path = (entry.findtext("FilePath") or "").replace("\\", "/")
            if not path.lower().startswith("../cm7/application/"):
                continue
            filename = path.rsplit("/", 1)[-1]
            if filename in SHARED_APPLICATION or entry.findtext("FileType") == "5":
                if filename in SHARED_APPLICATION:
                    retained.add(filename)
            else:
                entries.remove(entry)
    if retained != SHARED_APPLICATION:
        raise ValueError(f"Missing shared Application units: {SHARED_APPLICATION - retained}")

    source_root = ROOT / "CM7/ApplicationV3"
    sources = {path.relative_to(source_root).as_posix() for path in source_root.rglob("*.c")}
    if sources != EXPECTED_V3:
        raise ValueError(f"Unexpected V3 source set: {sources ^ EXPECTED_V3}")
    group = ET.SubElement(groups, "Group")
    ET.SubElement(group, "GroupName").text = "Application V3"
    entries = ET.SubElement(group, "Files")
    for source in sorted((*sources, *(path.relative_to(source_root).as_posix()
                                      for path in source_root.rglob("*.h")))):
        entry = ET.SubElement(entries, "File")
        ET.SubElement(entry, "FileName").text = source.rsplit("/", 1)[-1]
        ET.SubElement(entry, "FileType").text = "1" if source.endswith(".c") else "5"
        ET.SubElement(entry, "FilePath").text = "../CM7/ApplicationV3/" + source

    ET.indent(tree, space="  ")
    # Refuse to replace an existing generated project or any user project.
    with DESTINATION.open("xb") as project_file:
        tree.write(project_file, encoding="utf-8", xml_declaration=True)
    print(DESTINATION)


if __name__ == "__main__":
    generate()
