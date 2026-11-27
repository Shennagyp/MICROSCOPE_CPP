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
StageOperationError error(StageModelError e) { return {e,std::nullopt}; }
} // namespace

NanometreRange::NanometreRange(Nanometres a,Nanometres b) noexcept:min_(a),max_(b){}
core::Result<NanometreRange,StageModelError> NanometreRange::create(Nanometres a,Nanometres b){
    if(a>=b)return core::Result<NanometreRange,StageModelError>::failure(StageModelError::InvalidRange);
    return core::Result<NanometreRange,StageModelError>::success(NanometreRange(a,b));
}
Nanometres NanometreRange::minimum()const noexcept{return min_;}
Nanometres NanometreRange::maximum()const noexcept{return max_;}
bool NanometreRange::contains(Nanometres v)const noexcept{return v>=min_&&v<=max_;}

RadianRange::RadianRange(Radians a,Radians b)noexcept:min_(a),max_(b){}
core::Result<RadianRange,StageModelError> RadianRange::create(Radians a,Radians b){
    if(!finite(a)||!finite(b)||a>=b)
        return core::Result<RadianRange,StageModelError>::failure(StageModelError::InvalidAngularRange);
    return core::Result<RadianRange,StageModelError>::success(RadianRange(a,b));
}
Radians RadianRange::minimum()const noexcept{return min_;}
Radians RadianRange::maximum()const noexcept{return max_;}
bool RadianRange::contains(Radians v)const noexcept{return finite(v)&&v>=min_&&v<=max_;}

LinearAxisSpecification::LinearAxisSpecification(StageAxisId id,LinearAxisRole role,
    NanometreRange r,Nanometres inc,Nanometres a,Nanometres p)
    :id_(std::move(id)),role_(role),mechanicalRange_(r),minimumIncrement_(inc),accuracy_(a),precision_(p){}
core::Result<LinearAxisSpecification,StageModelError> LinearAxisSpecification::create(
    StageAxisId id,LinearAxisRole role,NanometreRange r,Nanometres inc,Nanometres a,Nanometres p){
    if(!validRole(role)||inc<=0||a<0||p<0||!r.contains(0))
        return core::Result<LinearAxisSpecification,StageModelError>::failure(StageModelError::InvalidAxisSpecification);
    return core::Result<LinearAxisSpecification,StageModelError>::success(
        LinearAxisSpecification(std::move(id),role,r,inc,a,p));
}
const StageAxisId& LinearAxisSpecification::id()const noexcept{return id_;}
LinearAxisRole LinearAxisSpecification::role()const noexcept{return role_;}
const NanometreRange& LinearAxisSpecification::mechanicalTravelRange()const noexcept{return mechanicalRange_;}
Nanometres LinearAxisSpecification::minimumIncrement()const noexcept{return minimumIncrement_;}
Nanometres LinearAxisSpecification::accuracy()const noexcept{return accuracy_;}
Nanometres LinearAxisSpecification::precision()const noexcept{return precision_;}

RotationAxisSpecification::RotationAxisSpecification(StageAxisId id,RotationAxisGeometry g,
    RadianRange r,Radians inc,Radians a,Radians p)
    :id_(std::move(id)),geometry_(g),mechanicalRange_(r),minimumIncrement_(inc),accuracy_(a),precision_(p){}
core::Result<RotationAxisSpecification,StageModelError> RotationAxisSpecification::create(
    StageAxisId id,RotationAxisGeometry g,RadianRange r,Radians inc,Radians a,Radians p){
    if(!validDirection(g.direction)||!finite(inc)||!finite(a)||!finite(p)||
       inc<=0||a<0||p<0||!r.contains(0))
        return core::Result<RotationAxisSpecification,StageModelError>::failure(StageModelError::InvalidAxisSpecification);
    return core::Result<RotationAxisSpecification,StageModelError>::success(
        RotationAxisSpecification(std::move(id),g,r,inc,a,p));
}
const StageAxisId& RotationAxisSpecification::id()const noexcept{return id_;}
const RotationAxisGeometry& RotationAxisSpecification::geometry()const noexcept{return geometry_;}
const RadianRange& RotationAxisSpecification::mechanicalTravelRange()const noexcept{return mechanicalRange_;}
Radians RotationAxisSpecification::minimumIncrement()const noexcept{return minimumIncrement_;}
Radians RotationAxisSpecification::accuracy()const noexcept{return accuracy_;}
Radians RotationAxisSpecification::precision()const noexcept{return precision_;}

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
    :id_(std::move(id)),linearAxes_(std::move(l)),rotationAxes_(std::move(r)),slots_(std::move(s)){}
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
const std::vector<WorkingLinearAxis>& StageConfiguration::workingLinearAxes()const noexcept{return linearAxes_;}
const std::vector<WorkingRotationAxis>& StageConfiguration::workingRotationAxes()const noexcept{return rotationAxes_;}
const std::vector<StageSlot>& StageConfiguration::slots()const noexcept{return slots_;}

StageSpecification::StageSpecification(EquipmentComponentId id,std::vector<LinearAxisSpecification> l,
    std::vector<RotationAxisSpecification> r,std::vector<StageConfiguration> c)
    :id_(std::move(id)),linearAxes_(std::move(l)),rotationAxes_(std::move(r)),configurations_(std::move(c)){}
core::Result<StageSpecification,StageModelError> StageSpecification::create(
    EquipmentComponentId id,std::vector<LinearAxisSpecification> l,
    std::vector<RotationAxisSpecification> r,std::vector<StageConfiguration> c){
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
const EquipmentComponentId& StageSpecification::componentId()const noexcept{return id_;}
const std::vector<LinearAxisSpecification>& StageSpecification::linearAxes()const noexcept{return linearAxes_;}
const std::vector<RotationAxisSpecification>& StageSpecification::rotationAxes()const noexcept{return rotationAxes_;}
const std::vector<StageConfiguration>& StageSpecification::configurations()const noexcept{return configurations_;}
const StageConfiguration* StageSpecification::configuration(const StageConfigurationId& id)const noexcept{
    const auto i=std::find_if(configurations_.begin(),configurations_.end(),[&](const auto& v){return v.id()==id;});
    return i==configurations_.end()?nullptr:&*i;
}
const LinearAxisSpecification* StageSpecification::linearAxis(const StageAxisId& id)const noexcept{
    const auto i=std::find_if(linearAxes_.begin(),linearAxes_.end(),[&](const auto& v){return v.id()==id;});
    return i==linearAxes_.end()?nullptr:&*i;
}
const LinearAxisSpecification* StageSpecification::linearAxis(LinearAxisRole role)const noexcept{
    const auto i=std::find_if(linearAxes_.begin(),linearAxes_.end(),[&](const auto& v){return v.role()==role;});
    return i==linearAxes_.end()?nullptr:&*i;
}
const RotationAxisSpecification* StageSpecification::rotationAxis(const StageAxisId& id)const noexcept{
    const auto i=std::find_if(rotationAxes_.begin(),rotationAxes_.end(),[&](const auto& v){return v.id()==id;});
    return i==rotationAxes_.end()?nullptr:&*i;
}

bool StagePositionSnapshot::xyKnown()const noexcept{return x.has_value()&&y.has_value();}
bool StagePositionSnapshot::xyzKnown()const noexcept{return x.has_value()&&y.has_value()&&z.has_value();}

Stage::Stage(StageSpecification s,std::shared_ptr<IStageDriver> d,StageConfigurationId c)
    :specification_(std::move(s)),driver_(std::move(d)),activeConfigurationId_(std::move(c)){}
core::Result<Stage,StageModelError> Stage::create(StageSpecification s,
    std::shared_ptr<IStageDriver> d,StageConfigurationId c){
    if(!d)return core::Result<Stage,StageModelError>::failure(StageModelError::InvalidDriver);
    if(!s.configuration(c))return core::Result<Stage,StageModelError>::failure(StageModelError::ConfigurationNotFound);
    return core::Result<Stage,StageModelError>::success(Stage(std::move(s),std::move(d),std::move(c)));
}
const WorkingLinearAxis* Stage::workingLinearAxis(LinearAxisRole role)const noexcept{
    return findWorking(activeConfiguration(),specification_,role);
}
void Stage::invalidatePosition()noexcept{position_=StagePositionSnapshot{};}
core::Status<StageOperationError> Stage::driverFailure(const StageDriverError& e){
    return core::Status<StageOperationError>::failure({StageModelError::DriverFailure,e});
}
core::Status<StageOperationError> Stage::home(){
    const auto& cfg=activeConfiguration();
    for(const auto& a:cfg.workingLinearAxes()){
        auto r=driver_->homeLinearAxis(a.axisId);
        if(!r){invalidatePosition();return driverFailure(r.error());}
    }
    for(const auto& a:cfg.workingRotationAxes()){
        auto r=driver_->homeRotationAxis(a.axisId);
        if(!r){invalidatePosition();return driverFailure(r.error());}
    }
    position_=StagePositionSnapshot{};
    position_.x=0;position_.y=0;position_.z=0;
    for(const auto& a:cfg.workingRotationAxes())position_.rotations.push_back({a.axisId,0.0});
    position_.evidence=StagePositionEvidence::HomingReference;
    return core::Status<StageOperationError>::success();
}
core::Status<StageOperationError> Stage::refreshPosition(){
    if(!position_.xyzKnown()||position_.rotations.size()!=activeConfiguration().workingRotationAxes().size())
        return core::Status<StageOperationError>::failure(error(StageModelError::PositionUnknown));
    StageAbsolutePosition saved;
    saved.linear={*position_.x,*position_.y,*position_.z};saved.rotations=position_.rotations;
    auto h=home();if(!h)return h;
    return moveTo(saved);
}
core::Status<StageOperationError> Stage::moveTo(SlotRelativePosition p,const StageSlotId& id){
    if(!position_.xyKnown())
        return core::Status<StageOperationError>::failure(error(StageModelError::PositionUnknown));
    const auto& cfg=activeConfiguration();
    const auto s=std::find_if(cfg.slots().begin(),cfg.slots().end(),[&](const auto& v){return v.id()==id;});
    if(s==cfg.slots().end()||!s->xRange().contains(p.x)||!s->yRange().contains(p.y))
        return core::Status<StageOperationError>::failure(error(StageModelError::TargetOutsideSlot));
    Nanometres x=0,y=0;
    if(!add(s->originInStageCoordinates().x,p.x,x)||!add(s->originInStageCoordinates().y,p.y,y))
        return core::Status<StageOperationError>::failure(error(StageModelError::CoordinateOverflow));
    const auto* wx=workingLinearAxis(LinearAxisRole::X);
    const auto* wy=workingLinearAxis(LinearAxisRole::Y);
    if(!wx||!wy||!wx->effectiveRange.contains(x)||!wy->effectiveRange.contains(y))
        return core::Status<StageOperationError>::failure(error(StageModelError::TargetOutsideWorkingRange));
    auto r=driver_->moveLinearAxesTo({{wx->axisId,x},{wy->axisId,y}});
    if(!r){invalidatePosition();return driverFailure(r.error());}
    position_.x=x;position_.y=y;position_.evidence=StagePositionEvidence::CommandDerived;
    return core::Status<StageOperationError>::success();
}
core::Status<StageOperationError> Stage::moveTo(const StageAbsolutePosition& p){
    const auto& cfg=activeConfiguration();
    if(!position_.xyzKnown()||position_.rotations.size()!=cfg.workingRotationAxes().size())
        return core::Status<StageOperationError>::failure(error(StageModelError::PositionUnknown));
    const auto* x=workingLinearAxis(LinearAxisRole::X);
    const auto* y=workingLinearAxis(LinearAxisRole::Y);
    const auto* z=workingLinearAxis(LinearAxisRole::Z);
    if(!x||!y||!z||!x->effectiveRange.contains(p.linear.x)||
       !y->effectiveRange.contains(p.linear.y)||!z->effectiveRange.contains(p.linear.z))
        return core::Status<StageOperationError>::failure(error(StageModelError::TargetOutsideWorkingRange));
    if(p.rotations.size()!=cfg.workingRotationAxes().size())
        return core::Status<StageOperationError>::failure(error(StageModelError::RotationTargetMismatch));
    std::set<StageAxisId> ids;
    for(const auto& t:p.rotations){
        const auto* w=findWorkingRotation(cfg,t.axisId);
        if(!w||!w->effectiveRange.contains(t.angle)||!ids.insert(t.axisId).second)
            return core::Status<StageOperationError>::failure(error(StageModelError::RotationTargetMismatch));
    }
    auto l=driver_->moveLinearAxesTo({{x->axisId,p.linear.x},{y->axisId,p.linear.y},{z->axisId,p.linear.z}});
    if(!l){invalidatePosition();return driverFailure(l.error());}
    if(!p.rotations.empty()){
        auto r=driver_->moveRotationAxesTo(p.rotations);
        if(!r){invalidatePosition();return driverFailure(r.error());}
    }
    position_.x=p.linear.x;position_.y=p.linear.y;position_.z=p.linear.z;
    position_.rotations=p.rotations;position_.evidence=StagePositionEvidence::CommandDerived;
    return core::Status<StageOperationError>::success();
}
core::Status<StageOperationError> Stage::moveBy(const StageDisplacement& d){
    if(!position_.xyzKnown()||position_.rotations.size()!=activeConfiguration().workingRotationAxes().size())
        return core::Status<StageOperationError>::failure(error(StageModelError::PositionUnknown));
    StageAbsolutePosition p;
    if(!add(*position_.x,d.linear.x,p.linear.x)||!add(*position_.y,d.linear.y,p.linear.y)||
       !add(*position_.z,d.linear.z,p.linear.z))
        return core::Status<StageOperationError>::failure(error(StageModelError::CoordinateOverflow));
    if(d.rotations.size()!=position_.rotations.size())
        return core::Status<StageOperationError>::failure(error(StageModelError::RotationTargetMismatch));
    for(const auto& current:position_.rotations){
        const auto delta=std::find_if(d.rotations.begin(),d.rotations.end(),
            [&](const auto& v){return v.axisId==current.axisId;});
        if(delta==d.rotations.end()||!finite(current.angle+delta->displacement))
            return core::Status<StageOperationError>::failure(error(StageModelError::RotationTargetMismatch));
        p.rotations.push_back({current.axisId,current.angle+delta->displacement});
    }
    return moveTo(p);
}
core::Status<StageOperationError> Stage::activateConfiguration(const StageConfigurationId& id){
    const auto* next=specification_.configuration(id);
    if(!next)return core::Status<StageOperationError>::failure(error(StageModelError::ConfigurationNotFound));
    if(id==activeConfigurationId_)return core::Status<StageOperationError>::success();
    const auto* old=specification_.configuration(activeConfigurationId_);
    bool same=old&&old->workingLinearAxes().size()==next->workingLinearAxes().size()&&
        old->workingRotationAxes().size()==next->workingRotationAxes().size();
    if(same){
        for(const auto& a:old->workingLinearAxes())
            if(!findWorking(*next,a.axisId)){same=false;break;}
        for(const auto& a:old->workingRotationAxes())
            if(!findWorkingRotation(*next,a.axisId)){same=false;break;}
    }
    activeConfigurationId_=id;
    if(!same)invalidatePosition();
    return core::Status<StageOperationError>::success();
}
const StageSpecification& Stage::specification()const noexcept{return specification_;}
const StageConfiguration& Stage::activeConfiguration()const noexcept{
    return *specification_.configuration(activeConfigurationId_);
}
const StagePositionSnapshot& Stage::currentPosition()const noexcept{return position_;}
std::optional<StageSlotId> Stage::currentSlotId()const noexcept{
    if(!position_.xyKnown())return std::nullopt;
    std::optional<StageSlotId> found;
    for(const auto& s:activeConfiguration().slots()){
        Nanometres x0=0,x1=0,y0=0,y1=0;
        if(!bounds(s,x0,x1,y0,y1))return std::nullopt;
        if(*position_.x>=x0&&*position_.x<=x1&&*position_.y>=y0&&*position_.y<=y1){
            if(found)return std::nullopt;
            found=s.id();
        }
    }
    return found;
}
} // namespace microscope::controller
