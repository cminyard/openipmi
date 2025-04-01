
git clone git://git.yoctoproject.org/poky
cd poky
git checkout scarthgap
cd ..
git clone git://git.openembedded.org/meta-openembedded
cd meta-openembedded
git checkout scarthgap
cd ..

# handle local.conf
# Add test program layer

cd poky
source oe-init-build-env

bitbake-layers add-layer ../meta-openembedded/meta-oe

bitbake core-image-minimal
