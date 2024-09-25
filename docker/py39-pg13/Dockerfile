FROM postgres:13
RUN apt-get update && apt-get install -y \
	build-essential \
	clang \
	libpython3.9-dev \
	python3.9-dev \
	postgresql-client-13 \
	postgresql-server-dev-13 \
	python3-setuptools

ENV PGUSER postgres
ENV PYTHON_OVERRIDE python3.9
ENV LC_ALL C.UTF-8
