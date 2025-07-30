FROM postgres:14
RUN apt-get update && apt-get install -y \
	build-essential \
	clang \
	libpython3-dev \
	python3-dev \
	postgresql-client-14 \
	postgresql-server-dev-14 \
	python3-setuptools

ENV PGUSER=postgres
ENV PYTHON_OVERRIDE=python3.11
ENV LC_ALL=C.UTF-8
