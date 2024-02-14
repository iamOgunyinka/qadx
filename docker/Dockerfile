FROM debian:11

# Install dependencies and tools.
RUN apt update
RUN apt install -y build-essential cmake g++-10 gcc-10 git libdrm-dev
RUN apt install -y libgles-dev libpng-dev libwayland-dev libweston-9-dev
RUN apt install -y make patch pkg-config weston wget libboost-dev

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 10 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 10

# Build wayland-ivi-extension to get ilm dependencies.
RUN git clone https://github.com/GENIVI/wayland-ivi-extension.git && cd wayland-ivi-extension && \
    git checkout 2.3.0 && \
    mkdir build && cd build && cmake .. && make install
