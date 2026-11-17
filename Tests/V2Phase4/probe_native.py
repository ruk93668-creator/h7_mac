from pathlib import Path
import os,sys
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'output/V2Phase4/native';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT);sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));sys.path.insert(0,str(ROOT/'output/V2Phase1/python_deps'))
import run_tests as a
a.cc+=['--split_sections','-I',ROOT/'CM7/Device/Components/OV5640']
a.build('native',[ROOT/'Tests/V2Phase4/native_probe.c',ROOT/'Tests/V2Phase4/native_dependencies.c',ROOT/'CM7/ApplicationV2/Modules/Camera/camera_format.c',ROOT/'CM7/ApplicationV2/Core/media_format.c',ROOT/'CM7/Device/Src/ov5640_device.c',ROOT/'CM7/Media/Src/camera_capture.c'],'Native_Run')
for i in range(2):print(a.execute('native','Native_Run',i))
