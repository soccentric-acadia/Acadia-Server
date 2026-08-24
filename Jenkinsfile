// Acadia-Server — Jenkins declarative pipeline.
// Build + unit/integration tests in a pinned Ubuntu container with a
// disposable PostgreSQL service for the integration suite.
pipeline {
    agent {
        docker {
            image 'ubuntu:22.04'
            args '-u root'
        }
    }

    environment {
        ACADIA_GIT_READ_URL = "${env.ACADIA_GIT_READ_URL ?: 'https://github.com/soccentric-acadia/Acadia-HAL.git'}"
    }

    stages {
        stage('Start PostgreSQL') {
            steps {
                sh 'docker run -d --rm --name acadia-test-pg -e POSTGRES_DB=acadia_test -e POSTGRES_USER=acadia -e POSTGRES_PASSWORD=acadia_test -p 5432:5432 postgres:16-alpine'
                sh 'until docker exec acadia-test-pg pg_isready -U acadia >/dev/null 2>&1; do sleep 1; done'
            }
        }
        stage('Dependencies') {
            steps {
                sh 'apt-get update -qq && apt-get install -y --no-install-recommends build-essential cmake ninja-build git pkg-config protobuf-compiler libprotobuf-dev libprotoc-dev libgrpc++-dev libgrpc-dev libssl-dev libjsoncpp-dev uuid-dev zlib1g-dev libbrotli-dev libpq-dev libc-ares-dev libspdlog-dev libgtest-dev libargon2-dev'
                sh '''git clone --depth=1 --branch 0.8.0 https://github.com/jbeder/yaml-cpp.git /tmp/yaml-cpp
                       cmake -S /tmp/yaml-cpp -B /tmp/yaml-cpp/build -DCMAKE_INSTALL_PREFIX=/usr -DYAML_CPP_BUILD_TESTS=OFF -GNinja
                       ninja -C /tmp/yaml-cpp/build install'''
                sh '''git clone --depth=1 --branch v1.9.3 https://github.com/drogonframework/drogon.git /tmp/drogon
                       cd /tmp/drogon && git submodule update --init --recursive
                       cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DBUILD_CTL=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DUSE_POSTGRESQL=ON -DUSE_MYSQL=OFF -DUSE_SQLITE3=OFF -GNinja
                       ninja -C build install'''
            }
        }
        stage('Checkout HAL protos') {
            steps { sh 'git clone --depth=1 $ACADIA_GIT_READ_URL ../Acadia-HAL' }
        }
        stage('Build') {
            steps {
                sh 'cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON'
                sh 'ninja -C build -j$(nproc)'
            }
        }
        stage('Test') {
            environment {
                ACADIA_TEST_DB = 'postgres://acadia:acadia_test@localhost:5432/acadia_test'
                ACADIA_TEST_DSN = 'postgres://acadia:acadia_test@localhost:5432/acadia_test'
            }
            steps {
                sh 'ctest --test-dir build -j$(nproc) --output-on-failure -L unit'
                sh 'ctest --test-dir build -j$(nproc) --output-on-failure -L integration'
            }
        }
    }

    post {
        always {
            sh 'docker rm -f acadia-test-pg || true'
        }
    }
}
