FROM python:3.12-slim
WORKDIR /app
COPY pyproject.toml ./
COPY lora_tracker_archiver ./lora_tracker_archiver
RUN pip install --no-cache-dir .
RUN useradd --create-home --uid 10001 archiver && mkdir -p /data && chown archiver:archiver /data
USER archiver
ENV DATABASE_PATH=/data/lora-tracker-history.sqlite3
CMD ["python", "-m", "lora_tracker_archiver"]
