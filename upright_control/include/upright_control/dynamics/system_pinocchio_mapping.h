#pragma once

#include <ocs2_pinocchio_interface/PinocchioStateInputMapping.h>

#include <upright_control/dimensions.h>
#include <upright_control/types.h>

namespace upright {

template <typename Scalar>
class TripleIntegratorPinocchioMapping final
    : public ocs2::PinocchioStateInputMapping<Scalar> {
   public:
    using VecXs = VecX<Scalar>;
    using MatXs = MatX<Scalar>;

    TripleIntegratorPinocchioMapping(const RobotDimensions& dims) : dims_(dims) {}

    ~TripleIntegratorPinocchioMapping() override = default;

    TripleIntegratorPinocchioMapping<Scalar>* clone() const override {
        return new TripleIntegratorPinocchioMapping<Scalar>(*this);
    }

    VecXs getPinocchioJointPosition(const VecXs& state) const override {
        return state.head(dims_.q);
    }

    VecXs getPinocchioJointVelocity(const VecXs& state,
                                    const VecXs& input) const override {
        return state.segment(dims_.q, dims_.v);
    }

    VecXs getPinocchioJointAcceleration(const VecXs& state,
                                        const VecXs& input) const override {
        return state.tail(dims_.v);
    }

    // Maps the Jacobians of an arbitrary function f w.r.t q and v (generalized
    // positions and velocities), as provided by Pinocchio as Jq and Jv, to the
    // Jacobian of the state dfdx and Jacobian of the input dfdu.
    std::pair<MatXs, MatXs> getOcs2Jacobian(const VecXs& state, const MatXs& Jq,
                                            const MatXs& Jv) const override {
        const auto output_dim = Jq.rows();
        MatXs dfdx(output_dim, Jq.cols() + Jv.cols() + dims_.v);
        dfdx << Jq, Jv, MatXs::Zero(output_dim, dims_.v);

        // NOTE: this isn't used for collision avoidance (which is the only
        // place this method is called)
        MatXs dfdu = MatXs::Zero(output_dim, dims_.u);

        return {dfdx, dfdu};
    }

   private:
    RobotDimensions dims_;
};

static const RobotDimensions OBSTACLE_DIMENSIONS{3, 3, 9, 0};

template <typename Scalar>
static const TripleIntegratorPinocchioMapping<Scalar> OBSTACLE_PINOCCHIO_MAPPING{
    OBSTACLE_DIMENSIONS};

template <typename Mapping, typename Scalar>
class SystemPinocchioMapping final
    : public ocs2::PinocchioStateInputMapping<Scalar> {
   public:
    using VecXs = VecX<Scalar>;
    using MatXs = MatX<Scalar>;

    explicit SystemPinocchioMapping(const OptimizationDimensions& dims)
        : dims_(dims), robot_mapping_(dims.robot) {}

    ~SystemPinocchioMapping() override = default;

    SystemPinocchioMapping<Mapping, Scalar>* clone() const override {
        return new SystemPinocchioMapping<Mapping, Scalar>(*this);
    }

    VecXs getPinocchioJointPosition(const VecXs& state) const override {
        VecXs q_pin(dims_.q());

        // Pinocchio model order: robot joints first, then appended obstacles
        VecXs x_robot = state.head(dims_.robot.x);
        q_pin.head(dims_.robot.q) =
            robot_mapping_.getPinocchioJointPosition(x_robot);

        for (int i = 0; i < dims_.o; ++i) {
            VecXs x_obs = state.segment(dims_.robot.x + i * 9, 9);
            q_pin.segment(dims_.robot.q + i * 3, 3) =
                OBSTACLE_PINOCCHIO_MAPPING<Scalar>.getPinocchioJointPosition(
                    x_obs);
        }

        return q_pin;
    }

    VecXs getPinocchioJointVelocity(const VecXs& state,
                                    const VecXs& input) const override {
        VecXs v_pin(dims_.v());
        VecXs u_obs = VecXs::Zero(3);  // Obstacles have no input

        // Robot velocities first
        VecXs x_robot = state.head(dims_.robot.x);
        VecXs u_robot = input.head(dims_.robot.u);
        v_pin.head(dims_.robot.v) =
            robot_mapping_.getPinocchioJointVelocity(x_robot, u_robot);

        // Then obstacle velocities
        for (int i = 0; i < dims_.o; ++i) {
            VecXs x_obs = state.segment(dims_.robot.x + i * 9, 9);
            v_pin.segment(dims_.robot.v + i * 3, 3) =
                OBSTACLE_PINOCCHIO_MAPPING<Scalar>.getPinocchioJointVelocity(
                    x_obs, u_obs);
        }

        return v_pin;
    }

    VecXs getPinocchioJointAcceleration(const VecXs& state,
                                        const VecXs& input) const override {
        VecXs a_pin(dims_.v());
        VecXs u_obs = VecXs::Zero(3);  // Obstacles have no input

        // Robot accelerations first
        VecXs x_robot = state.head(dims_.robot.x);
        VecXs u_robot = input.head(dims_.robot.u);
        a_pin.head(dims_.robot.v) =
            robot_mapping_.getPinocchioJointAcceleration(x_robot, u_robot);

        // Then obstacle accelerations
        for (int i = 0; i < dims_.o; ++i) {
            VecXs x_obs = state.segment(dims_.robot.x + i * 9, 9);
            a_pin.segment(dims_.robot.v + i * 3, 3) =
                OBSTACLE_PINOCCHIO_MAPPING<Scalar>.getPinocchioJointAcceleration(
                    x_obs, u_obs);
        }

        return a_pin;
    }

    // Maps the Jacobians of an arbitrary function f w.r.t q and v (generalized
    // positions and velocities), as provided by Pinocchio as Jq_pin and
    // Jv_pin, to the Jacobian of the state dfdx and Jacobian of the input
    // dfdu.
    std::pair<MatXs, MatXs> getOcs2Jacobian(
        const VecXs& state, const MatXs& Jq_pin,
        const MatXs& Jv_pin) const override {
        const auto output_dim = Jq_pin.rows();
        MatXs dfdx(output_dim, dims_.x());
        MatXs dfdu(output_dim, dims_.u());

        // Robot contribution: robot columns are at the beginning
        VecXs x_robot = state.head(dims_.robot.x);
        MatXs Jq_pin_robot = Jq_pin.leftCols(dims_.robot.q);
        MatXs Jv_pin_robot = Jv_pin.leftCols(dims_.robot.v);
        MatXs dfdx_robot, dfdu_robot;
        std::tie(dfdx_robot, dfdu_robot) =
            robot_mapping_.getOcs2Jacobian(x_robot, Jq_pin_robot, Jv_pin_robot);

        dfdx.leftCols(dims_.robot.x) = dfdx_robot;
        dfdu.leftCols(dims_.robot.u) = dfdu_robot;

        // Obstacles follow the robot columns in Pinocchio order
        for (int i = 0; i < dims_.o; ++i) {
            VecXs x_obs = state.segment(dims_.robot.x + i * 9, 9);
            MatXs Jq_pin_obs = Jq_pin.middleCols(dims_.robot.q + i * 3, 3);
            MatXs Jv_pin_obs = Jv_pin.middleCols(dims_.robot.v + i * 3, 3);

            MatXs dfdx_obs;
            std::tie(dfdx_obs, std::ignore) =
                OBSTACLE_PINOCCHIO_MAPPING<Scalar>.getOcs2Jacobian(
                    x_obs, Jq_pin_obs, Jv_pin_obs);

            // Obstacles have no input, so no dfdu
            dfdx.middleCols(dims_.robot.x + i * 9, 9) = dfdx_obs;
        }

        return {dfdx, dfdu};
    }

   private:
    OptimizationDimensions dims_;
    Mapping robot_mapping_;
};

}  // namespace upright
