#!/usr/bin/env bash

container_id="$(docker container ls --filter 'ancestor=qadx-x86-image' --format '{{.Names}}' | head -n 1)"

if [ -n "$container_id" ]; then
  docker container attach "$container_id"
else
  docker build -t qadx-x86-image -f "docker/Dockerfile" .
  docker run -it --privileged --device=/dev/kvm --network host \
    -v "$(pwd):/src/" --workdir "/src/" --restart=unless-stopped \
    "qadx-x86-image" bash
fi

if [ -n "$1" ]; then
  string="$(docker container ls | tail -n +2)"
  id="$(echo "$string" | awk '{print $1}')"
  if [ -n "$id" ]; then
    echo "Removing container with ID: $id"
    docker container stop "$id"
    docker container rm "$id"
  fi

  sudo rm -rf build
fi
