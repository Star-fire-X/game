#!/bin/bash

# Legend2 Monitoring Stack Quick Start Script

set -e

echo "========================================"
echo "Legend2 Monitoring Stack Setup"
echo "========================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is not installed${NC}"
    echo "Please install Docker first: https://docs.docker.com/get-docker/"
    exit 1
fi

# Check if Docker Compose is installed
if ! command -v docker-compose &> /dev/null && ! docker compose version &> /dev/null; then
    echo -e "${RED}Error: Docker Compose is not installed${NC}"
    echo "Please install Docker Compose: https://docs.docker.com/compose/install/"
    exit 1
fi

# Get the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo -e "${GREEN}✓${NC} Docker and Docker Compose are installed"
echo ""

# Navigate to monitoring directory
cd "$SCRIPT_DIR"

# Check if docker-compose.yml exists
if [ ! -f "docker-compose.yml" ]; then
    echo -e "${RED}Error: docker-compose.yml not found${NC}"
    echo "Please ensure you are in the monitoring directory"
    exit 1
fi

echo -e "${YELLOW}Starting monitoring stack...${NC}"
echo ""

# Start services
docker-compose up -d

echo ""
echo -e "${GREEN}✓${NC} Monitoring stack started successfully!"
echo ""

# Wait for services to be healthy
echo -e "${YELLOW}Waiting for services to be ready...${NC}"
sleep 5

# Check service status
echo ""
echo "Service Status:"
echo "---------------"
docker-compose ps

echo ""
echo "========================================"
echo "Access Information"
echo "========================================"
echo ""
echo -e "${GREEN}Grafana:${NC}"
echo "  URL: http://localhost:3000"
echo "  Username: admin"
echo "  Password: admin"
echo "  (You will be prompted to change password on first login)"
echo ""
echo -e "${GREEN}Prometheus:${NC}"
echo "  URL: http://localhost:9090"
echo ""
echo -e "${GREEN}Alertmanager:${NC}"
echo "  URL: http://localhost:9093"
echo ""
echo "========================================"
echo "Next Steps"
echo "========================================"
echo ""
echo "1. Access Grafana at http://localhost:3000"
echo "2. Login with admin/admin"
echo "3. Navigate to Dashboards → Legend2 → Gateway Dashboard"
echo "4. Verify that metrics are being collected"
echo ""
echo "To view logs:"
echo "  docker-compose logs -f"
echo ""
echo "To stop services:"
echo "  docker-compose down"
echo ""
echo -e "${GREEN}Happy monitoring!${NC}"
