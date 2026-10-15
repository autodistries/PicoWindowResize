from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as out:
 subprocess.run(["javac","--release","8","-d",out,str(r/"src/research/pico/windowresize/ResizeMath.java"),str(r/"tests/ResizeTests.java")],check=True)
 subprocess.run(["java","-cp",out,"research.pico.windowresize.ResizeTests"],check=True)
 subprocess.run(["javac","--release","8","-d",out,*[str(r/"src/research/pico/windowresize"/name) for name in ["LatestRequests.java","SaveOrder.java"]],str(r/"tests/ReviewRegressionTests.java")],check=True)
 subprocess.run(["java","-cp",out,"research.pico.windowresize.ReviewRegressionTests"],check=True)
with tempfile.TemporaryDirectory(prefix="pico-native-drag-tests-") as output:
    target=Path(output)/"drag-tests"
    subprocess.run(["c++","-std=c++17","-O2",str(r/"tests/drag_host.cpp"),"-o",str(target)],check=True)
    subprocess.run([str(target)],check=True)
