FROM python:3.12-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY server.py .
USER 65534:65534
ENV BIND_ADDRESS=0.0.0.0
EXPOSE 8099
CMD ["python", "server.py"]
