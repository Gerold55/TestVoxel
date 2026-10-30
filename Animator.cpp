class PlayerAnimator {
public:
    void update(float deltaTime, bool isMoving, std::shared_ptr<Bone> rightArm, std::shared_ptr<Bone> leftArm, std::shared_ptr<Bone> rightLeg, std::shared_ptr<Bone> leftLeg) {
        if (isMoving) {
            // Swing arms and legs back and forth using time
            float animSpeed = 12.0f;
            float swingMax = 35.0f; // max rotation angle in degrees
            float swing = sin(glfwGetTime() * animSpeed) * swingMax;

            rightArm->rotation.x = swing;
            leftArm->rotation.x = -swing;
            rightLeg->rotation.x = -swing;
            leftLeg->rotation.x = swing;
        } else {
            // Smoothly return limbs to idle position
            rightArm->rotation.x = glm::mix(rightArm->rotation.x, 0.0f, deltaTime * 10.0f);
            leftArm->rotation.x = glm::mix(leftArm->rotation.x, 0.0f, deltaTime * 10.0f);
            rightLeg->rotation.x = glm::mix(rightLeg->rotation.x, 0.0f, deltaTime * 10.0f);
            leftLeg->rotation.x = glm::mix(leftLeg->rotation.x, 0.0f, deltaTime * 10.0f);
        }
    }
};