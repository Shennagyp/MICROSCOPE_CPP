#include "controller/stage/Stage.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
namespace {
using namespace microscope;
using namespace microscope::controller;
int failures=0;
void check(bool v,const char* m){if(!v){++failures;std::cerr<<"FAIL: "<<m<<'\n';}}
template<class T,class E>T take(core::Result<T,E> r){
    if(!r)throw std::runtime_error("Unexpected factory failure.");return std::move(r).value();
}
core::StageAxisId aid(const char*s){return take(core::StageAxisId::create(s));}
core::StageConfigurationId cid(const char*s){return take(core::StageConfigurationId::create(s));}
core::StageSlotId sid(const char*s){return take(core::StageSlotId::create(s));}
core::EquipmentComponentId eid(const char*s){return take(core::EquipmentComponentId::create(s));}
NanometreRange nr(Nanometres a,Nanometres b){return take(NanometreRange::create(a,b));}
RadianRange rr(Radians a,Radians b){return take(RadianRange::create(a,b));}
class FakeDriver final:public IStageDriver{
public:
 core::Status<StageDriverError> homeLinearAxis(const StageAxisId&i)override{linearHomed.push_back(i);return core::Status<StageDriverError>::success();}
 core::Status<StageDriverError> homeRotationAxis(const StageAxisId&i)override{rotaryHomed.push_back(i);return core::Status<StageDriverError>::success();}
 core::Status<StageDriverError> moveLinearAxesTo(const std::vector<LinearAxisTarget>&v)override{linearMoves.push_back(v);return core::Status<StageDriverError>::success();}
 core::Status<StageDriverError> moveRotationAxesTo(const std::vector<RotationAxisTarget>&v)override{rotaryMoves.push_back(v);return core::Status<StageDriverError>::success();}
 std::vector<StageAxisId> linearHomed,rotaryHomed;
 std::vector<std::vector<LinearAxisTarget>> linearMoves;
 std::vector<std::vector<RotationAxisTarget>> rotaryMoves;
};
StageSpecification fixture(StageConfigurationId& slides,StageConfigurationId& petri,StageSlotId& slot2){
 auto xid=aid("x"),yid=aid("y"),zid=aid("z"),tid=aid("theta");
 auto x=take(LinearAxisSpecification::create(xid,LinearAxisRole::X,nr(0,1'000'000'000),100,2000,500));
 auto y=take(LinearAxisSpecification::create(yid,LinearAxisRole::Y,nr(0,500'000'000),100,2000,500));
 auto z=take(LinearAxisSpecification::create(zid,LinearAxisRole::Z,nr(0,100'000'000),50,1000,250));
 auto t=take(RotationAxisSpecification::create(tid,
  RotationAxisGeometry{{10'000,20'000,30'000},RotationAxisDirection::Z},
  rr(-3.141592653589793,3.141592653589793),0.0001,0.001,0.0002));
 slides=cid("slides"),petri=cid("petri"),slot2=sid("slot-2");
 std::vector<StageSlot> slideSlots;
 for(int i=0;i<10;++i){
  const auto name="slide-slot-"+std::to_string(i+1);
  slideSlots.push_back(take(StageSlot::create(sid(name.c_str()),
   {static_cast<Nanometres>(i)*80'000'000,0},nr(0,75'000'000),nr(0,25'000'000))));
 }
 auto sc=take(StageConfiguration::create(slides,
  {{xid,nr(0,800'000'000)},{yid,nr(0,300'000'000)},{zid,nr(8'000'000,80'000'000)}},
  {},std::move(slideSlots)));
 auto petriSlot=take(StageSlot::create(sid("petri-slot"),{0,0},nr(0,150'000'000),nr(0,150'000'000)));
 auto pc=take(StageConfiguration::create(petri,
  {{xid,nr(0,800'000'000)},{yid,nr(0,300'000'000)},{zid,nr(2'000'000,60'000'000)}},
  {{tid,rr(-1.5707963267948966,1.5707963267948966)}},{std::move(petriSlot)}));
 return take(StageSpecification::create(eid("stage"),
  {std::move(x),std::move(y),std::move(z)},{std::move(t)},{std::move(sc),std::move(pc)}));
}
void testModelAndConfigurationCardinality(){
 StageConfigurationId slides=cid("s0"),petri=cid("p0");StageSlotId s2=sid("s2");
 auto spec=fixture(slides,petri,s2);
 check(spec.configuration(slides)->slots().size()==10,"configuration supports ten generic slots");
 check(spec.configuration(petri)->slots().size()==1,"configuration supports one sample-holder slot");
 check(spec.configuration(slides)->workingRotationAxes().empty(),"rotation is optional per configuration");
 check(spec.configuration(petri)->workingRotationAxes().size()==1,"another configuration enables rotation");
 check(spec.configuration(slides)->workingLinearAxes()[2].effectiveRange.minimum()==8'000'000,
       "effective Z range is configuration-specific");
}
void testMovementAndCurrentSlot(){
 auto d=std::make_shared<FakeDriver>();StageConfigurationId slides=cid("s0"),petri=cid("p0");StageSlotId s2=sid("s2");
 auto stage=take(Stage::create(fixture(slides,petri,s2),d,slides));
 check(static_cast<bool>(stage.home()),"home active linear axes");
 check(d->linearHomed.size()==3&&d->rotaryHomed.empty(),"does not home unused rotation axis");
 check(static_cast<bool>(stage.moveTo({12'000'000,10'000'000},s2)),"slot-relative move accepted");
 const auto& target=d->linearMoves.back();
 check(target.size()==2&&target[0].position==92'000'000&&target[1].position==10'000'000,
       "slot-local point translated to global stage coordinates");
 check(stage.currentSlotId()==s2,"current slot is derived from global position");
 check(!stage.moveTo({76'000'000,1},s2),"rejects movement outside slot travel region");
}
void testRefreshPosition(){
 auto d=std::make_shared<FakeDriver>();StageConfigurationId slides=cid("s0"),petri=cid("p0");StageSlotId s2=sid("s2");
 auto stage=take(Stage::create(fixture(slides,petri,s2),d,slides));
 check(static_cast<bool>(stage.home()),"initial home");
 check(static_cast<bool>(stage.moveTo({4'000'000,5'000'000},s2)),"set XY");
 StageAbsolutePosition p;p.linear={84'000'000,5'000'000,12'000'000};
 check(static_cast<bool>(stage.moveTo(p)),"set absolute XYZ");
 auto n=d->linearMoves.size();
 check(static_cast<bool>(stage.refreshPosition()),"refresh and restore position");
 check(d->linearHomed.size()==6&&d->linearMoves.size()==n+1,"refresh homes then returns");
 check(stage.currentPosition().x==84'000'000&&stage.currentPosition().y==5'000'000&&
       stage.currentPosition().z==12'000'000,"restores previous machine coordinates");
}
void testRotationActivation(){
 auto d=std::make_shared<FakeDriver>();StageConfigurationId slides=cid("s0"),petri=cid("p0");StageSlotId s2=sid("s2");
 auto stage=take(Stage::create(fixture(slides,petri,s2),d,slides));
 check(static_cast<bool>(stage.home()),"home slide configuration");
 check(static_cast<bool>(stage.activateConfiguration(petri)),"activate Petri configuration on same Stage");
 check(static_cast<bool>(stage.home()),"home axes used by Petri configuration");
 check(d->rotaryHomed.size()==1,"rotation homed only when active configuration uses it");
 StageAbsolutePosition p;p.linear={1'000'000,1'000'000,10'000'000};p.rotations.push_back({aid("theta"),0.25});
 check(static_cast<bool>(stage.moveTo(p)),"move absolute XYZ and rotary axis");
 check(d->rotaryMoves.size()==1&&std::abs(d->rotaryMoves[0][0].angle-0.25)<1e-12,
       "rotation command expressed in radians");
}
}
int main(){
 testModelAndConfigurationCardinality();testMovementAndCurrentSlot();
 testRefreshPosition();testRotationActivation();
 if(failures){std::cerr<<failures<<" Controller Stage test(s) failed.\n";return 1;}
 std::cout<<"All Controller Stage tests passed.\n";return 0;
}
