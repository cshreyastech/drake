#include "drake/multibody/tree/universal_mobilizer.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "drake/common/eigen_types.h"
#include "drake/math/rotation_matrix.h"
#include "drake/multibody/tree/body_node_impl.h"

namespace drake {
namespace multibody {
namespace internal {

using std::cos;
using std::sin;

template <typename T>
UniversalMobilizer<T>::~UniversalMobilizer() = default;

template <typename T>
std::unique_ptr<BodyNode<T>> UniversalMobilizer<T>::CreateBodyNode(
    const BodyNode<T>* parent_node, const RigidBody<T>* body,
    const Mobilizer<T>* mobilizer) const {
  return std::make_unique<BodyNodeImpl<T, UniversalMobilizer>>(parent_node,
                                                               body, mobilizer);
}

template <typename T>
std::string UniversalMobilizer<T>::position_suffix(
    int position_index_in_mobilizer) const {
  switch (position_index_in_mobilizer) {
    case 0:
      return "qx";
    case 1:
      return "qy";
  }
  throw std::runtime_error("UniversalMobilizer has only 2 positions.");
}

template <typename T>
std::string UniversalMobilizer<T>::velocity_suffix(
    int velocity_index_in_mobilizer) const {
  switch (velocity_index_in_mobilizer) {
    case 0:
      return "wx";
    case 1:
      return "wy";
  }
  throw std::runtime_error("UniversalMobilizer has only 2 velocities.");
}

template <typename T>
Vector2<T> UniversalMobilizer<T>::get_angles(
    const systems::Context<T>& context) const {
  auto q = this->get_positions(context);
  DRAKE_ASSERT(q.size() == kNq);
  return q;
}

template <typename T>
const UniversalMobilizer<T>& UniversalMobilizer<T>::SetAngles(
    systems::Context<T>* context, const Vector2<T>& angles) const {
  auto q = this->GetMutablePositions(context);
  DRAKE_ASSERT(q.size() == kNq);
  q = angles;
  return *this;
}

template <typename T>
Vector2<T> UniversalMobilizer<T>::get_angular_rates(
    const systems::Context<T>& context) const {
  const auto& v = this->get_velocities(context);
  DRAKE_ASSERT(v.size() == kNv);
  return v;
}

template <typename T>
const UniversalMobilizer<T>& UniversalMobilizer<T>::SetAngularRates(
    systems::Context<T>* context, const Vector2<T>& angles_dot) const {
  auto v = this->GetMutableVelocities(context);
  DRAKE_ASSERT(v.size() == kNv);
  v = angles_dot;
  return *this;
}

template <typename T>
Eigen::Matrix<T, 3, 2> UniversalMobilizer<T>::CalcHwMatrix(
    const T* q, const T* v, Vector3<T>* Hw_dot) const {
  DRAKE_ASSERT(q != nullptr);
  const T s = sin(q[0]);
  const T c = cos(q[0]);
  // The Hw matrix is defined as Hw = [Fx_F, My_F] where Fx_F is the unit x
  // vector and My_F simply picks off the middle column of R_FI(θ₀) because My_M
  // is the unit y vector.
  Eigen::Matrix<T, 3, 2> H;
  H.col(0) = Vector3<T>::UnitX();    // Fx_F
  H.col(1) = Vector3<T>(0.0, c, s);  // My_F
  if (Hw_dot != nullptr) {
    DRAKE_ASSERT(v != nullptr);
    // Since only the second column of Hw evolves with time, we only return that
    // column as a vector. The vector is the time derivative of My_F.
    *Hw_dot = Vector3<T>(0, -s * v[0], c * v[0]);
  }
  return H;
}

template <typename T>
math::RigidTransform<T> UniversalMobilizer<T>::CalcAcrossMobilizerTransform(
    const systems::Context<T>& context) const {
  const auto& q = this->get_positions(context);
  DRAKE_ASSERT(q.size() == kNq);
  return calc_X_FM(q.data());
}

template <typename T>
SpatialVelocity<T> UniversalMobilizer<T>::CalcAcrossMobilizerSpatialVelocity(
    const systems::Context<T>& context,
    const Eigen::Ref<const VectorX<T>>& v) const {
  DRAKE_ASSERT(v.size() == kNv);
  const auto& q = this->get_positions(context);
  DRAKE_ASSERT(q.size() == kNq);
  return calc_V_FM(q.data(), v.data());
}

template <typename T>
SpatialAcceleration<T>
UniversalMobilizer<T>::CalcAcrossMobilizerSpatialAcceleration(
    const systems::Context<T>& context,
    const Eigen::Ref<const VectorX<T>>& vdot) const {
  const auto& q = this->get_positions(context);
  DRAKE_ASSERT(q.size() == kNq);
  const auto& v = this->get_velocities(context);
  DRAKE_ASSERT(v.size() == kNv && vdot.size() == kNv);
  return calc_A_FM(q.data(), v.data(), vdot.data());
}

template <typename T>
void UniversalMobilizer<T>::ProjectSpatialForce(
    const systems::Context<T>& context, const SpatialForce<T>& F_BMo_F,
    Eigen::Ref<VectorX<T>> tau) const {
  DRAKE_ASSERT(tau.size() == kNv);
  const auto& q = this->get_positions(context);
  DRAKE_ASSERT(q.size() == kNq);
  calc_tau(q.data(), F_BMo_F, tau.data());
}

template <typename T>
void UniversalMobilizer<T>::DoCalcNMatrix(const systems::Context<T>&,
                                          EigenPtr<MatrixX<T>> N) const {
  *N = Matrix2<T>::Identity();
}

template <typename T>
void UniversalMobilizer<T>::DoCalcNplusMatrix(
    const systems::Context<T>&, EigenPtr<MatrixX<T>> Nplus) const {
  *Nplus = Matrix2<T>::Identity();
}

template <typename T>
void UniversalMobilizer<T>::DoCalcNDotMatrix(const systems::Context<T>&,
                                             EigenPtr<MatrixX<T>> Ndot) const {
  *Ndot = Matrix2<T>::Zero();
}

template <typename T>
void UniversalMobilizer<T>::DoCalcNplusDotMatrix(
    const systems::Context<T>&, EigenPtr<MatrixX<T>> NplusDot) const {
  *NplusDot = Matrix2<T>::Zero();
}

template <typename T>
void UniversalMobilizer<T>::MapVelocityToQDot(
    const systems::Context<T>&, const Eigen::Ref<const VectorX<T>>& v,
    EigenPtr<VectorX<T>> qdot) const {
  DRAKE_ASSERT(v.size() == kNv);
  DRAKE_ASSERT(qdot != nullptr);
  DRAKE_ASSERT(qdot->size() == kNq);
  *qdot = v;
}

template <typename T>
void UniversalMobilizer<T>::MapQDotToVelocity(
    const systems::Context<T>&, const Eigen::Ref<const VectorX<T>>& qdot,
    EigenPtr<VectorX<T>> v) const {
  DRAKE_ASSERT(qdot.size() == kNq);
  DRAKE_ASSERT(v != nullptr);
  DRAKE_ASSERT(v->size() == kNv);
  *v = qdot;
}

template <typename T>
template <typename ToScalar>
std::unique_ptr<Mobilizer<ToScalar>>
UniversalMobilizer<T>::TemplatedDoCloneToScalar(
    const MultibodyTree<ToScalar>& tree_clone) const {
  const Frame<ToScalar>& inboard_frame_clone =
      tree_clone.get_variant(this->inboard_frame());
  const Frame<ToScalar>& outboard_frame_clone =
      tree_clone.get_variant(this->outboard_frame());
  return std::make_unique<UniversalMobilizer<ToScalar>>(
      tree_clone.get_mobod(this->mobod().index()), inboard_frame_clone,
      outboard_frame_clone);
}

template <typename T>
std::unique_ptr<Mobilizer<double>> UniversalMobilizer<T>::DoCloneToScalar(
    const MultibodyTree<double>& tree_clone) const {
  return TemplatedDoCloneToScalar(tree_clone);
}

template <typename T>
std::unique_ptr<Mobilizer<AutoDiffXd>> UniversalMobilizer<T>::DoCloneToScalar(
    const MultibodyTree<AutoDiffXd>& tree_clone) const {
  return TemplatedDoCloneToScalar(tree_clone);
}

template <typename T>
std::unique_ptr<Mobilizer<symbolic::Expression>>
UniversalMobilizer<T>::DoCloneToScalar(
    const MultibodyTree<symbolic::Expression>& tree_clone) const {
  return TemplatedDoCloneToScalar(tree_clone);
}

}  // namespace internal
}  // namespace multibody
}  // namespace drake

DRAKE_DEFINE_CLASS_TEMPLATE_INSTANTIATIONS_ON_DEFAULT_SCALARS(
    class ::drake::multibody::internal::UniversalMobilizer);
