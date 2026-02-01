#!/bin/bash

# Legend2 Monitoring Validation Script

set -e

echo "========================================"
echo "Legend2 Monitoring Validation"
echo "========================================"
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SUCCESS_COUNT=0
FAIL_COUNT=0

# Function to check service health
check_service() {
    local service_name=$1
    local url=$2
    local expected_code=${3:-200}

    echo -n "Checking $service_name... "

    if response=$(curl -s -o /dev/null -w "%{http_code}" "$url" 2>/dev/null); then
        if [ "$response" = "$expected_code" ]; then
            echo -e "${GREEN}✓ OK${NC} (HTTP $response)"
            SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
            return 0
        else
            echo -e "${RED}✗ FAILED${NC} (HTTP $response, expected $expected_code)"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            return 1
        fi
    else
        echo -e "${RED}✗ FAILED${NC} (Cannot connect)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        return 1
    fi
}

# Function to check Prometheus metrics
check_metrics() {
    local metric_name=$1

    echo -n "Checking metric '$metric_name'... "

    if result=$(curl -s "http://localhost:9090/api/v1/query?query=$metric_name" 2>/dev/null | grep -o '"status":"success"'); then
        if [ -n "$result" ]; then
            echo -e "${GREEN}✓ Found${NC}"
            SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
            return 0
        fi
    fi

    echo -e "${YELLOW}⚠ Not found${NC} (metric may not be exposed yet)"
    return 0
}

echo "1. Service Health Checks"
echo "------------------------"
check_service "Prometheus" "http://localhost:9090/-/healthy"
check_service "Grafana" "http://localhost:3000/api/health"
check_service "Alertmanager" "http://localhost:9093/-/healthy"

echo ""
echo "2. Docker Container Status"
echo "--------------------------"
if docker ps --filter "name=legend2-" --format "table {{.Names}}\t{{.Status}}" | grep -q "Up"; then
    echo -e "${GREEN}✓${NC} Containers are running:"
    docker ps --filter "name=legend2-" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
    SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
else
    echo -e "${RED}✗${NC} No running containers found"
    FAIL_COUNT=$((FAIL_COUNT + 1))
fi

echo ""
echo "3. Prometheus Targets Status"
echo "-----------------------------"
if targets=$(curl -s "http://localhost:9090/api/v1/targets" 2>/dev/null); then
    echo "$targets" | grep -q '"health":"up"' && {
        echo -e "${GREEN}✓${NC} Some targets are UP"
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    } || {
        echo -e "${YELLOW}⚠${NC} No targets are UP (this is normal if gateway is not running)"
    }
else
    echo -e "${RED}✗${NC} Cannot query Prometheus targets"
    FAIL_COUNT=$((FAIL_COUNT + 1))
fi

echo ""
echo "4. Gateway Metrics Availability"
echo "--------------------------------"
echo "(These checks will show warnings if gateway is not running)"
check_metrics "gateway_forward_total"
check_metrics "gateway_route_table_connection_count"
check_metrics "gateway_user_register"
check_metrics "gateway_service_connected_world"

echo ""
echo "5. Grafana Datasource Status"
echo "----------------------------"
if datasources=$(curl -s -u admin:admin "http://localhost:3000/api/datasources" 2>/dev/null); then
    if echo "$datasources" | grep -q '"type":"prometheus"'; then
        echo -e "${GREEN}✓${NC} Prometheus datasource is configured"
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    else
        echo -e "${RED}✗${NC} Prometheus datasource not found"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
else
    echo -e "${YELLOW}⚠${NC} Cannot check datasources (may need authentication)"
fi

echo ""
echo "6. Grafana Dashboard Availability"
echo "----------------------------------"
if dashboards=$(curl -s -u admin:admin "http://localhost:3000/api/search?type=dash-db" 2>/dev/null); then
    if echo "$dashboards" | grep -q "Gateway Dashboard"; then
        echo -e "${GREEN}✓${NC} Gateway Dashboard is loaded"
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    else
        echo -e "${YELLOW}⚠${NC} Gateway Dashboard not found (may take a moment to load)"
    fi
else
    echo -e "${YELLOW}⚠${NC} Cannot check dashboards (may need authentication)"
fi

echo ""
echo "========================================"
echo "Validation Summary"
echo "========================================"
echo -e "Successful checks: ${GREEN}$SUCCESS_COUNT${NC}"
echo -e "Failed checks: ${RED}$FAIL_COUNT${NC}"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}✓ All core services are healthy!${NC}"
    echo ""
    echo "Next steps:"
    echo "1. Ensure your gateway service is running and exposing metrics on port 9101"
    echo "2. Access Grafana at http://localhost:3000"
    echo "3. Check the Gateway Dashboard for real-time metrics"
    exit 0
else
    echo -e "${YELLOW}⚠ Some checks failed. Please review the output above.${NC}"
    echo ""
    echo "Troubleshooting:"
    echo "- Check logs: docker-compose logs -f"
    echo "- Restart services: docker-compose restart"
    echo "- Verify configuration files are correct"
    exit 1
fi
