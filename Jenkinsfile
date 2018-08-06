pipeline {
    agent any

    environment {
        PROJECT = 'raft_core_grpc'
        CONAN_CHANNEL = 'testing'
        CONAN_USER = 'sds'
        CONAN_PASS = credentials('CONAN_PASS')
    }

    stages {
        stage('Get Version') {
            steps {
                script {
                    TAG = sh(script: "grep version conanfile.py | awk '{print \$3}' | tr -d '\n' | tr -d '\"'", returnStdout: true)
                }
            }
        }

        stage('Build') {
            steps {
                sh "docker build --rm --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT} ."
            }
        }

        stage('Test') {
            steps {
                echo "Tests go here"
            }
        }

        stage('Deploy') {
            when {
                branch "${CONAN_CHANNEL}/*"
            }
            steps {
                sh "docker run --rm ${PROJECT}"
            }
        }
    }

    post {
        always {
            sh "docker rmi ${PROJECT}"
        }
    }
}
