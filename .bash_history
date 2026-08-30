pkg install virglrenderer-android
virgl_test_server_android
virgl_test_server_android &
pkill virgl
virgl_test_server_android > /dev/null 2>&1 &
proot-distro login ubuntu
pkg update && pkg install git -y
cd /sdcard/Projeler
git clone https://github.com/Apoman46/WinLatoRMaliVulkan.git repo-temp
cp -r WinlatorMali-bionic-mali-1.1/* repo-temp/
cp -r WinlatorMali-bionic-mali-1.1/.[^.]* repo-temp/ 2>/dev/null
cd repo-temp
mkdir -p .github/workflows
nano .github/workflows/build.yml
git add .
git commit -m "WinlatorMali mali-tuned upload"
git push
git config --global --add safe.directory /storage/emulated/0/Projeler/repo-temp
git add .
git commit -m "WinlatorMali mali-tuned upload"
git push
git commit -m "WinlatorMali mali-tuned upload"
git add .
git commit -m "WinlatorMali mali-tuned upload"
git add .
git commit -m "WinlatorMali mali-tuned upload"
git push
git config --global user.email "babapiyoabi002@gmail.com"
git config --global user.name "Apoman46"
git add .
git commit -m "WinlatorMali mali-tuned upload"
git push
