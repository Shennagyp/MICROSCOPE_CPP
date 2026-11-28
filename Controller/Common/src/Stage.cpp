#include "controller/stage/Stage.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace microscope::controller {
namespace {
bool finite(Radians v) noexcept { return std::isfinite(v); }
bool validRole(LinearAxisRole v) noexcept {
    return v==LinearAxisRole::X || v==LinearAxisRole::Y || v==LinearAxisRole::Z;
}
bool validDirection(RotationAxisDirection v) noexcept {
    return v==RotationAxisDirection::X || v==RotationAxisDirection::Y ||
           v==RotationAxisDirection::Z;
}
bool add(Nanometres a, Nanometres b, Nanometres& out) noexcept {
    if ((b>0 && a>std::numeric_limits<Nanometres>::max()-b) ||
        (b<0 && a<std::numeric_limits<Nanometres>::min()-b)) return false;
    out=a+b; return true;
}
bool within(const NanometreRange& a,const NanometreRange& b) noexcept {
    return a.contains(b.minimum()) && a.contains(b.maximum());
}
bool within(const RadianRange& a,const RadianRange& b) noexcept {
    return a.contains(b.minimum()) && a.contains(b.maximum());
}
bool bounds(const StageSlot& s,Nanometres& x0,Nanometres& x1,
    Nanometres& y0,Nanometres& y1) noexcept {
    return add(s.originInStageCoordinates().x,s.xRange().minimum(),x0) &&
        add(s.originInStageCoordinates().x,s.xRange().maximum(),x1) &&
        add(s.originInStageCoordinates().y,s.yRange().minimum(),y0) &&
        add(s.originInStageCoordinates().y,s.yRange().maximum(),y1);
}
bool overlaps(const StageSlot& a,const StageSlot& b) noexcept {
    Nanometres ax0=0,ax1=0,ay0=0,ay1=0,bx0=0,bx1=0,by0=0,by1=0;
    if(!bounds(a,ax0,ax1,ay0,ay1)||!bounds(b,bx0,bx1,by0,by1)) return true;
    return ax0<bx1 && bx0<ax1 && ay0<by1 && by0<ay1;
}
const WorkingLinearAxis* findWorking(const StageConfiguration& c,
    const StageSpecification& s,LinearAxisRole role) noexcept {
    for(const auto& w:c.workingLinearAxes()) {
        const auto* axis=s.linearAxis(w.axisId);
        if(axis && axis->role()==role) return &w;
    }
    return nullptr;
}
const WorkingLinearAxis* findWorking(const StageConfiguration& c,
    const StageAxisId& id) noexcept {
    const auto i=std::find_if(c.workingLinearAxes().begin(),c.workingLinearAxes().end(),
        [&id](const WorkingLinearAxis& w){return w.axisId==id;});
    return i==c.workingLinearAxes().end()?nullptr:&*i;
}
const WorkingRotationAxis* findWorkingRotation(const StageConfiguration& c,
    const StageAxisId& id) noexcept {
    const auto i=std::find_if(c.workingRotationAxes().begin(),c.workingRotationAxes().end(),
        [&id](const WorkingRotationAxis& w){return w.axisId==id;});
    return i==c.workingRotationAxes().end()?nullptr:&*i;
}
bool samePlan(const StageMotionPlan& left,const StageMotionPlan& right) noexcept {
    if(left.sequence!=right.sequence ||
       left.configurationId!=right.configurationId ||
       left.action!=right.action ||
       left.axesToHome!=right.axesToHome ||
       left.refreshRestoration!=right.refreshRestoration ||
       left.linearTargets.size()!=right.linearTargets.size() ||
       left.rotationTargets.size()!=right.rotationTargets.size()) return false;
    for(std::size_t i=0;i<left.linearTargets.size();++i) {
        if(left.linearTargets[i].axisId!=right.linearTargets[i].axisId ||
           left.linearTargets[i].position!=right.linearTargets[i].position) return false;
    }
    for(std::size_t i=0;i<left.rotationTargets.size();++i) {
        if(left.rotationTargets[i].axisId!=right.rotationTargets[i].axisId ||
           left.rotationTargets[i].angle!=right.rotationTargets[i].angle) return false;
    }
    return true;
}
bool sameAxisSet(
    const core::StageAxisSet& actual,
    const std::vector<StageAxisId>& expected) {
    if(actual.axes().size()!=expected.size()) return false;
    std::set<StageAxisId> ids(expected.begin(),expected.end());
    if(ids.size()!=expected.size()) return false;
    for(const auto& id:actual.axes()) {
        if(ids.erase(id)!=1) return false;
    }
    return ids.empty();
}

} // namespace

NanometreRange::NanometreRange(Nanometres a,Nanometres b) noexcept:minimum_(a),maximum_(b){}
core::Result<NanometreRange,StageModelError> NanometreRange::create(Nanometres a,Nanometres b){
    if(a>=b)return core::Result<NanometreRange,StageModelError>::failure(StageModelError::InvalidRange);
    return core::Result<NanometreRange,StageModelError>::success(NanometreRange(a,b));
}
Nanometres NanometreRange::minimum()const noexcept{return minimum_;}
Nanometres NanometreRange::maximum()const noexcept{return maximum_;}
bool NanometreRange::contains(Nanometres v)const noexcept{return v>=minimum_&&v<=maximum_;}

RadianRange::RadianRange(Radians a,Radians b)noexcept:minimum_(a),maximum_(b){}
core::Result<RadianRange,StageModelError> RadianRange::create(Radians a,Radians b){
    if(!finite(a)||!finite(b)||a>=b)
        return core::Result<RadianRange,StageModelError>::failure(StageModelError::InvalidAngularRange);
    return core::Result<RadianRange,StageModelError>::success(RadianRange(a,b));
}
Radians RadianRange::minimum()const noexcept{return minimum_;}
Radians RadianRange::maximum()const noexcept{return maximum_;}
bool RadianRange::contains(Radians v)const noexcept{return finite(v)&&v>=minimum_&&v<=maximum_;}

LinearAxis::LinearAxis(StageAxisId id,LinearAxisRole role,
    NanometreRange r,Nanometres inc,Nanometres a,Nanometres p)
    :id_(std::move(id)),role_(role),mechanicalTravelRange_(r),minimumIncrement_(inc),accuracy_(a),precision_(p){}
core::Result<LinearAxis,StageModelError> LinearAxis::create(
    StageAxisId id,LinearAxisRole role,NanometreRange r,Nanometres inc,Nanometres a,Nanometres p){
    if(!validRole(role)||inc<=0||a<0||p<0||!r.contains(0))
        return core::Result<LinearAxis,StageModelError>::failure(StageModelError::InvalidAxisSpecification);
    return core::Result<LinearAxis,StageModelError>::success(
        LinearAxis(std::move(id),role,r,inc,a,p));
}
const StageAxisId& LinearAxis::id()const noexcept{return id_;}
LinearAxisRole LinearAxis::role()const noexcept{return role_;}
const NanometreRange& LinearAxis::mechanicalTravelRange()const noexcept{return mechanicalTravelRange_;}
Nanometres LinearAxis::minimumIncrement()const noexcept{return minimumIncrement_;}
Nanometres LinearAxis::accuracy()const noexcept{return accuracy_;}
Nanometres LinearAxis::precision()const noexcept{return precision_;}

StageAxisId LinearAxis::homeCommand() const { return id_; }
core::Result<LinearAxisTarget,StageModelError> LinearAxis::moveTo(Nanometres target) const {
    if(!mechanicalTravelRange_.contains(target))
        return core::Result<LinearAxisTarget,StageModelError>::failure(StageModelError::TargetOutsideWorkingRange);
    return core::Result<LinearAxisTarget,StageModelError>::success({id_,target});
}
core::Result<LinearAxisTarget,StageModelError> LinearAxis::moveBy(
    Nanometres current,Nanometres displacement) const {
    Nanometres target=0;
    if(!add(current,displacement,target))
        return core::Result<LinearAxisTarget,StageModelError>::failure(StageModelError::CoordinateOverflow);
    return moveTo(target);
}

RotationAxis::RotationAxis(StageAxisId id,RotationAxisGeometry g,
    RadianRange r,Radians inc,Radians a,Radians p)
    :id_(std::move(id)),geometry_(g),mechanicalTravelRange_(r),minimumIncrement_(inc),accuracy_(a),precision_(p){}
core::Result<RotationAxis,StageModelError> RotationAxis::create(
    StageAxisId id,RotationAxisGeometry g,RadianRange r,Radians inc,Radians a,Radians p){
    if(!validDirection(g.direction)||!finite(inc)||!finite(a)||!finite(p)||
       inc<=0||a<0||p<0||!r.contains(0))
        return core::Result<RotationAxis,StageModelError>::failure(StageModelError::InvalidAxisSpecification);
    return core::Result<RotationAxis,StageModelError>::success(
        RotationAxis(std::move(id),g,r,inc,a,p));
}
const StageAxisId& RotationAxis::id()const noexcept{return id_;}
const RotationAxisGeometry& RotationAxis::geometry()const noexcept{return geometry_;}
const RadianRange& RotationAxis::mechanicalTravelRange()const noexcept{return mechanicalTravelRange_;}
Radians RotationAxis::minimumIncrement()const noexcept{return minimumIncrement_;}
Radians RotationAxis::accuracy()const noexcept{return accuracy_;}
Radians RotationAxis::precision()const noexcept{return precision_;}

StageAxisId RotationAxis::homeCommand() const { return id_; }
core::Result<RotationAxisTarget,StageModelError> RotationAxis::rotateTo(Radians target) const {
    if(!mechanicalTravelRange_.contains(target))
        return core::Result<RotationAxisTarget,StageModelError>::failure(StageModelError::TargetOutsideWorkingRange);
    return core::Result<RotationAxisTarget,StageModelError>::success({id_,target});
}
core::Result<RotationAxisTarget,StageModelError> RotationAxis::rotateBy(
    Radians current,Radians displacement) const {
    const Radians target=current+displacement;
    if(!finite(target))
        return core::Result<RotationAxisTarget,StageModelError>::failure(StageModelError::RotationTargetMismatch);
    return rotateTo(target);
}

StageSlot::StageSlot(StageSlotId id,StagePosition2D o,NanometreRange x,NanometreRange y)
    :id_(std::move(id)),origin_(o),xRange_(x),yRange_(y){}
core::Result<StageSlot,StageModelError> StageSlot::create(
    StageSlotId id,StagePosition2D o,NanometreRange x,NanometreRange y){
    Nanometres unused=0;
    if(x.minimum()<0||y.minimum()<0)
        return core::Result<StageSlot,StageModelError>::failure(StageModelError::InvalidSlotRange);
    if(!add(o.x,x.maximum(),unused)||!add(o.y,y.maximum(),unused))
        return core::Result<StageSlot,StageModelError>::failure(StageModelError::CoordinateOverflow);
    return core::Result<StageSlot,StageModelError>::success(StageSlot(std::move(id),o,x,y));
}
const StageSlotId& StageSlot::id()const noexcept{return id_;}
const StagePosition2D& StageSlot::originInStageCoordinates()const noexcept{return origin_;}
const NanometreRange& StageSlot::xRange()const noexcept{return xRange_;}
const NanometreRange& StageSlot::yRange()const noexcept{return yRange_;}

StageConfiguration::StageConfiguration(StageConfigurationId id,std::vector<WorkingLinearAxis> l,
    std::vector<WorkingRotationAxis> r,std::vector<StageSlot> s)
    :id_(std::move(id)),workingLinearAxes_(std::move(l)),
     workingRotationAxes_(std::move(r)),slots_(std::move(s)){}
core::Result<StageConfiguration,StageModelError> StageConfiguration::create(
    StageConfigurationId id,std::vector<WorkingLinearAxis> l,
    std::vector<WorkingRotationAxis> r,std::vector<StageSlot> s){
    if(s.empty())return core::Result<StageConfiguration,StageModelError>::failure(StageModelError::NoSlots);
    std::set<StageAxisId> axisIds;
    for(const auto& a:l)if(!axisIds.insert(a.axisId).second)
        return core::Result<StageConfiguration,StageModelError>::failure(StageModelError::DuplicateWorkingAxis);
    for(const auto& a:r)if(!axisIds.insert(a.axisId).second)
        return core::Result<StageConfiguration,StageModelError>::failure(StageModelError::DuplicateWorkingAxis);
    std::set<StageSlotId> slotIds;
    for(std::size_t i=0;i<s.size();++i){
        if(!slotIds.insert(s[i].id()).second)
            return core::Result<StageConfiguration,StageModelError>::failure(StageModelError::DuplicateSlotId);
        for(std::size_t j=i+1;j<s.size();++j)if(overlaps(s[i],s[j]))
            return core::Result<StageConfiguration,StageModelError>::failure(StageModelError::OverlappingSlots);
    }
    return core::Result<StageConfiguration,StageModelError>::success(
        StageConfiguration(std::move(id),std::move(l),std::move(r),std::move(s)));
}
const StageConfigurationId& StageConfiguration::id()const noexcept{return id_;}
const std::vector<WorkingLinearAxis>& StageConfiguration::workingLinearAxes()const noexcept{return workingLinearAxes_;}
const std::vector<WorkingRotationAxis>& StageConfiguration::workingRotationAxes()const noexcept{return workingRotationAxes_;}
const std::vector<StageSlot>& StageConfiguration::slots()const noexcept{return slots_;}

StageSpecification::StageSpecification(EquipmentComponentId id,std::vector<LinearAxis> l,
    std::vector<RotationAxis> r,std::vector<StageConfiguration> c)
    :componentId_(std::move(id)),linearAxes_(std::move(l)),rotationAxes_(std::move(r)),configurations_(std::move(c)){}
core::Result<StageSpecification,StageModelError> StageSpecification::create(
    EquipmentComponentId id,std::vector<LinearAxis> l,
    std::vector<RotationAxis> r,std::vector<StageConfiguration> c){
    if(l.empty()||c.empty())return core::Result<StageSpecification,StageModelError>::failure(StageModelError::InvalidAxisSpecification);
    std::set<StageAxisId> ids;
    for(const auto& a:l)if(!ids.insert(a.id()).second)
        return core::Result<StageSpecification,StageModelError>::failure(StageModelError::DuplicateAxisId);
    for(const auto& a:r)if(!ids.insert(a.id()).second)
        return core::Result<StageSpecification,StageModelError>::failure(StageModelError::DuplicateAxisId);
    std::set<StageConfigurationId> configIds;
    for(const auto& cfg:c){
        if(!configIds.insert(cfg.id()).second)
            return core::Result<StageSpecification,StageModelError>::failure(StageModelError::DuplicateConfigurationId);
        const WorkingLinearAxis *x=nullptr,*y=nullptr,*z=nullptr;
        std::set<LinearAxisRole> roles;
        for(const auto& w:cfg.workingLinearAxes()){
            const auto ai=std::find_if(l.begin(),l.end(),[&](const auto& a){return a.id()==w.axisId;});
            if(ai==l.end())return core::Result<StageSpecification,StageModelError>::failure(StageModelError::UnknownWorkingAxis);
            if(!within(ai->mechanicalTravelRange(),w.effectiveRange))
                return core::Result<StageSpecification,StageModelError>::failure(StageModelError::EffectiveRangeOutsideMechanicalRange);
            if(!roles.insert(ai->role()).second)
                return core::Result<StageSpecification,StageModelError>::failure(StageModelError::DuplicateWorkingAxis);
            if(ai->role()==LinearAxisRole::X)x=&w;
            if(ai->role()==LinearAxisRole::Y)y=&w;
            if(ai->role()==LinearAxisRole::Z)z=&w;
        }
        if(!x)return core::Result<StageSpecification,StageModelError>::failure(StageModelError::MissingXWorkingAxis);
        if(!y)return core::Result<StageSpecification,StageModelError>::failure(StageModelError::MissingYWorkingAxis);
        if(!z)return core::Result<StageSpecification,StageModelError>::failure(StageModelError::MissingZWorkingAxis);
        for(const auto& w:cfg.workingRotationAxes()){
            const auto ai=std::find_if(r.begin(),r.end(),[&](const auto& a){return a.id()==w.axisId;});
            if(ai==r.end())return core::Result<StageSpecification,StageModelError>::failure(StageModelError::UnknownWorkingAxis);
            if(!within(ai->mechanicalTravelRange(),w.effectiveRange))
                return core::Result<StageSpecification,StageModelError>::failure(StageModelError::EffectiveRangeOutsideMechanicalRange);
        }
        for(const auto& s:cfg.slots()){
            Nanometres x0=0,x1=0,y0=0,y1=0;
            if(!bounds(s,x0,x1,y0,y1))
                return core::Result<StageSpecification,StageModelError>::failure(StageModelError::CoordinateOverflow);
            if(!x->effectiveRange.contains(x0)||!x->effectiveRange.contains(x1)||
               !y->effectiveRange.contains(y0)||!y->effectiveRange.contains(y1))
                return core::Result<StageSpecification,StageModelError>::failure(StageModelError::SlotOutsideWorkingRange);
        }
    }
    return core::Result<StageSpecification,StageModelError>::success(
        StageSpecification(std::move(id),std::move(l),std::move(r),std::move(c)));
}
const EquipmentComponentId& StageSpecification::componentId()const noexcept{return componentId_;}
const std::vector<LinearAxis>& StageSpecification::linearAxes()const noexcept{return linearAxes_;}
const std::vector<RotationAxis>& StageSpecification::rotationAxes()const noexcept{return rotationAxes_;}
const std::vector<StageConfiguration>& StageSpecification::configurations()const noexcept{return configurations_;}
const StageConfiguration* StageSpecification::configuration(const StageConfigurationId& id)const noexcept{
    const auto i=std::find_if(configurations_.begin(),configurations_.end(),[&](const auto& v){return v.id()==id;});
    return i==configurations_.end()?nullptr:&*i;
}
const LinearAxis* StageSpecification::linearAxis(const StageAxisId& id)const noexcept{
    const auto i=std::find_if(linearAxes_.begin(),linearAxes_.end(),[&](const auto& v){return v.id()==id;});
    return i==linearAxes_.end()?nullptr:&*i;
}
const LinearAxis* StageSpecification::linearAxis(LinearAxisRole role)const noexcept{
    const auto i=std::find_if(linearAxes_.begin(),linearAxes_.end(),[&](const auto& v){return v.role()==role;});
    return i==linearAxes_.end()?nullptr:&*i;
}
const RotationAxis* StageSpecification::rotationAxis(const StageAxisId& id)const noexcept{
    const auto i=std::find_if(rotationAxes_.begin(),rotationAxes_.end(),[&](const auto& v){return v.id()==id;});
    return i==rotationAxes_.end()?nullptr:&*i;
}

bool StagePositionSnapshot::xyKnown()const noexcept{return x.has_value()&&y.has_value();}
bool StagePositionSnapshot::xyzKnown()const noexcept{return x.has_value()&&y.has_value()&&z.has_value();}


Stage::Stage(StageSpecification specification,StageConfigurationId active)
    :specification_(std::move(specification)),activeConfigurationId_(std::move(active)){}

core::Result<Stage,StageModelError> Stage::create(
    StageSpecification specification,StageConfigurationId active) {
    if(!specification.configuration(active))
        return core::Result<Stage,StageModelError>::failure(StageModelError::ConfigurationNotFound);
    return core::Result<Stage,StageModelError>::success(
        Stage(std::move(specification),std::move(active)));
}
const WorkingLinearAxis* Stage::workingLinearAxis(LinearAxisRole role) const noexcept {
    return findWorking(activeConfiguration(),specification_,role);
}

core::Result<StageMotionPlan,StageModelError> Stage::home() {
    if(pendingPlan_)
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::MotionInProgress);
    if(nextSequence_==std::numeric_limits<std::uint64_t>::max())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::MotionSequenceExhausted);
    StageMotionPlan plan;
    plan.sequence=nextSequence_++;
    plan.configurationId=activeConfigurationId_;
    plan.action=StageMotionAction::Home;
    for(const auto& working:activeConfiguration().workingLinearAxes()) {
        const auto* axis=specification_.linearAxis(working.axisId);
        if(!axis) return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::UnknownWorkingAxis);
        plan.axesToHome.push_back(axis->homeCommand());
    }
    for(const auto& working:activeConfiguration().workingRotationAxes()) {
        const auto* axis=specification_.rotationAxis(working.axisId);
        if(!axis) return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::UnknownWorkingAxis);
        plan.axesToHome.push_back(axis->homeCommand());
    }
    pendingPlan_=plan;
    return core::Result<StageMotionPlan,StageModelError>::success(std::move(plan));
}

core::Result<StageRefreshPlan,StageModelError> Stage::refreshPosition() {
    if(!position_.xyzKnown()||
       position_.rotations.size()!=activeConfiguration().workingRotationAxes().size())
        return core::Result<StageRefreshPlan,StageModelError>::failure(StageModelError::PositionUnknown);
    StageAbsolutePosition saved;
    saved.linear={*position_.x,*position_.y,*position_.z};
    saved.rotations=position_.rotations;
    auto homePlan=home();
    if(!homePlan)
        return core::Result<StageRefreshPlan,StageModelError>::failure(homePlan.error());
    return core::Result<StageRefreshPlan,StageModelError>::success(
        StageRefreshPlan{std::move(homePlan).value(),std::move(saved),activeConfigurationId_});
}

core::Result<StageMotionPlan,StageModelError> Stage::createMovePlan(
    std::vector<LinearAxisTarget> linearTargets,
    std::vector<RotationAxisTarget> rotationTargets,bool restoration) {
    if(pendingPlan_)
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::MotionInProgress);
    if(nextSequence_==std::numeric_limits<std::uint64_t>::max())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::MotionSequenceExhausted);
    if(linearTargets.empty())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::StaleMotionPlan);
    StageMotionPlan plan;
    plan.sequence=nextSequence_++;
    plan.configurationId=activeConfigurationId_;
    plan.action=StageMotionAction::MoveTo;
    plan.linearTargets=std::move(linearTargets);
    plan.rotationTargets=std::move(rotationTargets);
    plan.refreshRestoration=restoration;
    pendingPlan_=plan;
    return core::Result<StageMotionPlan,StageModelError>::success(std::move(plan));
}

core::Result<StageMotionPlan,StageModelError> Stage::restorePositionAfterRefresh(
    const StageRefreshPlan& refresh) {
    if(pendingPlan_||refresh.configurationId!=activeConfigurationId_||
       refresh.homePlan.action!=StageMotionAction::Home||
       !refresh.homePlan.configurationId||
       *refresh.homePlan.configurationId!=activeConfigurationId_||
       !lastCompletedPlan_||!samePlan(*lastCompletedPlan_,refresh.homePlan)||
       position_.evidence!=StagePositionEvidence::HomingReference)
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::RefreshNotReady);

    std::vector<LinearAxisTarget> linear;
    for(const auto role:{LinearAxisRole::X,LinearAxisRole::Y,LinearAxisRole::Z}) {
        const auto* axis=specification_.linearAxis(role);
        if(!axis) return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::UnknownWorkingAxis);
        const Nanometres target=role==LinearAxisRole::X?refresh.restorePosition.linear.x:
            role==LinearAxisRole::Y?refresh.restorePosition.linear.y:refresh.restorePosition.linear.z;
        auto command=axis->moveTo(target);
        if(!command) return core::Result<StageMotionPlan,StageModelError>::failure(command.error());
        linear.push_back(std::move(command).value());
    }

    std::vector<RotationAxisTarget> rotations;
    std::set<StageAxisId> supplied;
    for(const auto& target:refresh.restorePosition.rotations) {
        const auto* axis=specification_.rotationAxis(target.axisId);
        if(!axis||!findWorkingRotation(activeConfiguration(),target.axisId)||
           !supplied.insert(target.axisId).second)
            return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::RotationTargetMismatch);
        auto command=axis->rotateTo(target.angle);
        if(!command) return core::Result<StageMotionPlan,StageModelError>::failure(command.error());
        rotations.push_back(std::move(command).value());
    }
    if(supplied.size()!=activeConfiguration().workingRotationAxes().size())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::RotationTargetMismatch);
    return createMovePlan(std::move(linear),std::move(rotations),true);
}

core::Result<StageMotionPlan,StageModelError> Stage::moveTo(
    SlotRelativePosition position,const StageSlotId& slotId) {
    if(!position_.xyKnown())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::PositionUnknown);
    if(pendingPlan_)
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::MotionInProgress);

    const auto& config=activeConfiguration();
    const auto slot=std::find_if(config.slots().begin(),config.slots().end(),
        [&slotId](const StageSlot& value){return value.id()==slotId;});
    if(slot==config.slots().end()||!slot->xRange().contains(position.x)||
       !slot->yRange().contains(position.y))
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::TargetOutsideSlot);

    Nanometres x=0,y=0;
    if(!add(slot->originInStageCoordinates().x,position.x,x)||
       !add(slot->originInStageCoordinates().y,position.y,y))
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::CoordinateOverflow);

    const auto* xWorking=workingLinearAxis(LinearAxisRole::X);
    const auto* yWorking=workingLinearAxis(LinearAxisRole::Y);
    const auto* xAxis=specification_.linearAxis(LinearAxisRole::X);
    const auto* yAxis=specification_.linearAxis(LinearAxisRole::Y);
    if(!xWorking||!yWorking||!xAxis||!yAxis||
       !xWorking->effectiveRange.contains(x)||!yWorking->effectiveRange.contains(y))
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::TargetOutsideWorkingRange);
    auto xTarget=xAxis->moveTo(x),yTarget=yAxis->moveTo(y);
    if(!xTarget) return core::Result<StageMotionPlan,StageModelError>::failure(xTarget.error());
    if(!yTarget) return core::Result<StageMotionPlan,StageModelError>::failure(yTarget.error());
    return createMovePlan({std::move(xTarget).value(),std::move(yTarget).value()},{});
}

core::Result<StageMotionPlan,StageModelError> Stage::moveTo(
    const StageAbsolutePosition& position) {
    if(!position_.xyzKnown()||
       position_.rotations.size()!=activeConfiguration().workingRotationAxes().size())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::PositionUnknown);
    if(pendingPlan_)
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::MotionInProgress);

    std::vector<LinearAxisTarget> linear;
    for(const auto role:{LinearAxisRole::X,LinearAxisRole::Y,LinearAxisRole::Z}) {
        const auto* working=workingLinearAxis(role);
        const auto* axis=specification_.linearAxis(role);
        const Nanometres target=role==LinearAxisRole::X?position.linear.x:
            role==LinearAxisRole::Y?position.linear.y:position.linear.z;
        if(!working||!axis||!working->effectiveRange.contains(target))
            return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::TargetOutsideWorkingRange);
        auto command=axis->moveTo(target);
        if(!command) return core::Result<StageMotionPlan,StageModelError>::failure(command.error());
        linear.push_back(std::move(command).value());
    }

    if(position.rotations.size()!=activeConfiguration().workingRotationAxes().size())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::RotationTargetMismatch);
    std::set<StageAxisId> supplied;
    std::vector<RotationAxisTarget> rotations;
    for(const auto& target:position.rotations) {
        const auto* working=findWorkingRotation(activeConfiguration(),target.axisId);
        const auto* axis=specification_.rotationAxis(target.axisId);
        if(!working||!axis||!working->effectiveRange.contains(target.angle)||
           !supplied.insert(target.axisId).second)
            return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::RotationTargetMismatch);
        auto command=axis->rotateTo(target.angle);
        if(!command) return core::Result<StageMotionPlan,StageModelError>::failure(command.error());
        rotations.push_back(std::move(command).value());
    }
    if(supplied.size()!=activeConfiguration().workingRotationAxes().size())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::RotationTargetMismatch);
    return createMovePlan(std::move(linear),std::move(rotations));
}

core::Result<StageMotionPlan,StageModelError> Stage::moveBy(
    const StageDisplacement& displacement) {
    if(!position_.xyzKnown()||
       position_.rotations.size()!=activeConfiguration().workingRotationAxes().size())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::PositionUnknown);
    StageAbsolutePosition target;
    if(!add(*position_.x,displacement.linear.x,target.linear.x)||
       !add(*position_.y,displacement.linear.y,target.linear.y)||
       !add(*position_.z,displacement.linear.z,target.linear.z))
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::CoordinateOverflow);
    if(displacement.rotations.size()!=position_.rotations.size())
        return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::RotationTargetMismatch);

    std::set<StageAxisId> seen;
    for(const auto& current:position_.rotations) {
        const auto delta=std::find_if(displacement.rotations.begin(),displacement.rotations.end(),
            [&current](const RotationAxisDisplacement& value){return value.axisId==current.axisId;});
        const auto* axis=specification_.rotationAxis(current.axisId);
        if(delta==displacement.rotations.end()||!seen.insert(delta->axisId).second||!axis)
            return core::Result<StageMotionPlan,StageModelError>::failure(StageModelError::RotationTargetMismatch);
        auto next=axis->rotateBy(current.angle,delta->displacement);
        if(!next) return core::Result<StageMotionPlan,StageModelError>::failure(next.error());
        target.rotations.push_back(std::move(next).value());
    }
    return moveTo(target);
}

core::Status<StageModelError> Stage::validatePendingPlan(const StageMotionPlan& plan) const {
    if(!pendingPlan_||!samePlan(*pendingPlan_,plan)||
       !plan.configurationId||*plan.configurationId!=activeConfigurationId_)
        return core::Status<StageModelError>::failure(StageModelError::StaleMotionPlan);
    return core::Status<StageModelError>::success();
}

core::Status<StageModelError> Stage::validateForDispatch(const StageMotionPlan& plan) const {
    return validatePendingPlan(plan);
}
core::Status<StageModelError> Stage::cancelUndispatchedPlan(const StageMotionPlan& plan) {
    auto valid=validatePendingPlan(plan);
    if(!valid) return valid;
    pendingPlan_.reset();
    return core::Status<StageModelError>::success();
}

core::Status<StageModelError> Stage::recordCompletedMotion(
    const StageMotionPlan& plan,const core::StageMotionActionCompletion& completion) {
    auto valid=validatePendingPlan(plan);
    if(!valid) return valid;
    if(plan.action!=StageMotionAction::MoveTo||
       completion.action()!=core::StageMotionAction::MoveAxesTo||
       !plan.axesToHome.empty()||plan.linearTargets.empty())
        return core::Status<StageModelError>::failure(StageModelError::StaleMotionPlan);

    std::vector<StageAxisId> completionAxes;
    completionAxes.reserve(plan.linearTargets.size()+plan.rotationTargets.size());
    for(const auto& target:plan.linearTargets) completionAxes.push_back(target.axisId);
    for(const auto& target:plan.rotationTargets) completionAxes.push_back(target.axisId);
    if(!sameAxisSet(completion.axes(),completionAxes))
        return core::Status<StageModelError>::failure(StageModelError::StaleMotionPlan);

    // Validate against a copy first. Invalid or tampered completion payloads
    // must never partially mutate the authoritative position snapshot.
    StagePositionSnapshot nextPosition=position_;
    std::set<StageAxisId> linearIds;
    std::set<LinearAxisRole> linearRoles;
    for(const auto& target:plan.linearTargets) {
        const auto* axis=specification_.linearAxis(target.axisId);
        const auto* working=findWorking(activeConfiguration(),target.axisId);
        if(!axis||!working||!axis->mechanicalTravelRange().contains(target.position)||
           !linearIds.insert(target.axisId).second||
           !linearRoles.insert(axis->role()).second||
           (!plan.refreshRestoration&&!working->effectiveRange.contains(target.position)))
            return core::Status<StageModelError>::failure(StageModelError::StaleMotionPlan);
        if(axis->role()==LinearAxisRole::X) nextPosition.x=target.position;
        if(axis->role()==LinearAxisRole::Y) nextPosition.y=target.position;
        if(axis->role()==LinearAxisRole::Z) nextPosition.z=target.position;
    }

    const bool isSlotXYMove=linearRoles.size()==2&&
        linearRoles.count(LinearAxisRole::X)!=0&&linearRoles.count(LinearAxisRole::Y)!=0;
    const bool isFullXYZMove=linearRoles.size()==3&&
        linearRoles.count(LinearAxisRole::X)!=0&&linearRoles.count(LinearAxisRole::Y)!=0&&
        linearRoles.count(LinearAxisRole::Z)!=0;
    if((!isSlotXYMove&&!isFullXYZMove)||(plan.refreshRestoration&&!isFullXYZMove))
        return core::Status<StageModelError>::failure(StageModelError::StaleMotionPlan);

    std::set<StageAxisId> rotationIds;
    for(const auto& target:plan.rotationTargets) {
        const auto* axis=specification_.rotationAxis(target.axisId);
        const auto* working=findWorkingRotation(activeConfiguration(),target.axisId);
        if(!axis||!working||!axis->mechanicalTravelRange().contains(target.angle)||
           !rotationIds.insert(target.axisId).second||
           (!plan.refreshRestoration&&!working->effectiveRange.contains(target.angle)))
            return core::Status<StageModelError>::failure(StageModelError::StaleMotionPlan);
        const auto existing=std::find_if(nextPosition.rotations.begin(),nextPosition.rotations.end(),
            [&target](const RotationAxisTarget& value){return value.axisId==target.axisId;});
        if(existing==nextPosition.rotations.end()) nextPosition.rotations.push_back(target);
        else existing->angle=target.angle;
    }

    const auto expectedRotations=activeConfiguration().workingRotationAxes().size();
    if((isSlotXYMove&&!plan.rotationTargets.empty())||
       (isFullXYZMove&&rotationIds.size()!=expectedRotations))
        return core::Status<StageModelError>::failure(StageModelError::StaleMotionPlan);

    nextPosition.evidence=StagePositionEvidence::CommandDerived;
    position_=std::move(nextPosition);
    lastCompletedPlan_=plan;
    pendingPlan_.reset();
    return core::Status<StageModelError>::success();
}

core::Status<StageModelError> Stage::recordCompletedHome(
    const StageMotionPlan& plan,
    const core::StageMotionActionCompletion& completion,
    const core::StageHomeReferenceObservation& observation) {
    auto valid=validatePendingPlan(plan);
    if(!valid) return valid;
    if(plan.action!=StageMotionAction::Home||
       completion.action()!=core::StageMotionAction::HomeAxes||
       !plan.linearTargets.empty()||!plan.rotationTargets.empty()||plan.refreshRestoration)
        return core::Status<StageModelError>::failure(StageModelError::StaleMotionPlan);

    std::vector<StageAxisId> expected;
    expected.reserve(activeConfiguration().workingLinearAxes().size()+
        activeConfiguration().workingRotationAxes().size());
    for(const auto& axis:activeConfiguration().workingLinearAxes()) expected.push_back(axis.axisId);
    for(const auto& axis:activeConfiguration().workingRotationAxes()) expected.push_back(axis.axisId);
    auto expectedSet=core::StageAxisSet::create(std::move(expected));
    if(!expectedSet||!sameAxisSet(completion.axes(),expectedSet.value().axes())||
       !observation.matchesAxes(expectedSet.value()))
        return core::Status<StageModelError>::failure(
            StageModelError::InvalidHomeReferenceEvidence);

    StagePositionSnapshot nextPosition;
    nextPosition.x=0;
    nextPosition.y=0;
    nextPosition.z=0;
    for(const auto& working:activeConfiguration().workingRotationAxes())
        nextPosition.rotations.push_back({working.axisId,0.0});
    nextPosition.evidence=StagePositionEvidence::HomingReference;

    position_=std::move(nextPosition);
    lastCompletedPlan_=plan;
    pendingPlan_.reset();
    return core::Status<StageModelError>::success();
}

core::Status<StageModelError> Stage::recordFailedMotion(const StageMotionPlan& plan) {
    auto valid=validatePendingPlan(plan);
    if(!valid) return valid;
    invalidatePosition();
    pendingPlan_.reset();
    lastCompletedPlan_=plan;
    return core::Status<StageModelError>::success();
}

void Stage::invalidatePosition() noexcept { position_=StagePositionSnapshot{}; }

core::Status<StageModelError> Stage::activateConfiguration(const StageConfigurationId& id) {
    if(pendingPlan_)
        return core::Status<StageModelError>::failure(StageModelError::MotionInProgress);
    const auto* next=specification_.configuration(id);
    if(!next) return core::Status<StageModelError>::failure(StageModelError::ConfigurationNotFound);
    if(id==activeConfigurationId_) return core::Status<StageModelError>::success();
    const auto* old=specification_.configuration(activeConfigurationId_);
    bool same=old&&old->workingLinearAxes().size()==next->workingLinearAxes().size()&&
        old->workingRotationAxes().size()==next->workingRotationAxes().size();
    if(same) {
        for(const auto& axis:old->workingLinearAxes())
            if(!findWorking(*next,axis.axisId)){same=false;break;}
        for(const auto& axis:old->workingRotationAxes())
            if(!findWorkingRotation(*next,axis.axisId)){same=false;break;}
    }
    activeConfigurationId_=id;
    if(!same) invalidatePosition();
    return core::Status<StageModelError>::success();
}

const StageSpecification& Stage::specification() const noexcept{return specification_;}
const StageConfiguration& Stage::activeConfiguration() const noexcept {
    return *specification_.configuration(activeConfigurationId_);
}
const StagePositionSnapshot& Stage::currentPosition() const noexcept{return position_;}
std::optional<StageSlotId> Stage::currentSlotId() const noexcept {
    if(!position_.xyKnown())return std::nullopt;
    std::optional<StageSlotId> match;
    for(const auto& slot:activeConfiguration().slots()) {
        Nanometres minX=0,maxX=0,minY=0,maxY=0;
        if(!bounds(slot,minX,maxX,minY,maxY))return std::nullopt;
        if(*position_.x>=minX&&*position_.x<=maxX&&
           *position_.y>=minY&&*position_.y<=maxY) {
            if(match)return std::nullopt;
            match=slot.id();
        }
    }
    return match;
}
} // namespace microscope::controller
