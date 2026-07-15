FROM python:3.12-slim
WORKDIR /app
COPY pyproject.toml ./
COPY equine_archiver ./equine_archiver
RUN pip install --no-cache-dir .
RUN useradd --create-home --uid 10001 archiver && mkdir -p /data && chown archiver:archiver /data
USER archiver
ENV DATABASE_PATH=/data/equine-history.sqlite3
CMD ["python", "-m", "equine_archiver"]
