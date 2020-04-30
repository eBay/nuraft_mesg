pipeline {
    agent any

    environment {
        ORG = 'sds'
        CONAN_USER = 'sds'
        CONAN_PASS = credentials('CONAN_PASS')
        ARTIFACTORY_PASS = credentials('ARTIFACTORY_PASS')
        MASTER_BRANCH = 'develop'
        STABLE_BRANCH = 'testing/*'
    }

    stages {
        stage('Get Version') {
            steps {
                script {
                    PROJECT = sh(script: "grep -m 1 'name =' conanfile.py | awk '{print \$3}' | tr -d '\n' | tr -d '\"'", returnStdout: true)
                    CONAN_CHANNEL = sh(script: "echo ${BRANCH_NAME} | sed -E 's,(\\w+).*,\\1,' | tr -d '\n'", returnStdout: true)
                    TAG = sh(script: "grep -m 1 'version =' conanfile.py | awk '{print \$3}' | tr -d '\n' | tr -d '\"'", returnStdout: true)
                }
            }
        }

        stage('Build') {
            steps {
                sh "docker build --rm --build-arg BUILD_TYPE=debug --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg ARTIFACTORY_PASS=${ARTIFACTORY_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${GIT_COMMIT}-debug ."
                sh "docker build --rm --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg ARTIFACTORY_PASS=${ARTIFACTORY_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${GIT_COMMIT}-release ."
            }
        }

        stage('Deploy') {
            steps {
                sh "docker run --rm ${PROJECT}-${GIT_COMMIT}-nosanitize"
                sh "docker run --rm ${PROJECT}-${GIT_COMMIT}-release"
                slackSend channel: '#conan-pkgs', message: "*${PROJECT}/${TAG}@${CONAN_USER}/${CONAN_CHANNEL}* has been uploaded to conan repo."
            }
        }
    }

    post {
        always {
            sh "docker rmi -f ${PROJECT}-${GIT_COMMIT}-nosanitize"
            sh "docker rmi -f ${PROJECT}-${GIT_COMMIT}-release"
        }
    }
}
