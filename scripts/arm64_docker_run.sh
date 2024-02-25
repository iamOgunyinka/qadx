#!/usr/bin/env bash

container_id="$(docker container ls --filter 'ancestor=qadx-arm64-image' --format '{{.Names}}' | head -n 1)"

if [ -n "$container_id" ]; then
  docker container attach "$container_id"
else
  docker buildx build --platform linux/arm64/v8 -t qadx-arm64-image -f "docker/Dockerfile" .
  docker run -it --privileged --device=/dev/kvm --network host \
    -v "$(pwd):/src/" --workdir "/src/" --restart=unless-stopped \
    "qadx-arm64-image" bash
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
