import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import AgPlayer
TestCase {
 id: suite; name: "AboutPrivacy"; when: windowShown
 Component { id: hostComponent; Window { width: 850; height: 930; visible: true; color: Theme.background
 SettingsPage { objectName: "settingsUnderTest"; visible: true; selectedSection: 6 }
 } }
 function test_policy() {
  if (typeof testMainWindow !== "undefined") testMainWindow.hide()
  const host=createTemporaryObject(hostComponent,null); verify(host)
  host.requestActivate()
  const page=findChild(host,"settingsUnderTest"); verify(page)
  const button=findChild(page,"aboutPrivacyPolicyButton"); verify(button)
  verify(waitForRendering(page)); wait(300)
  if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput)
   grabImage(host.contentItem).save(visualFixtureOutput + "-about.png")
  mouseClick(button,button.width/2,button.height/2)
  const dialog=findChild(page,"privacyPolicyDialog"); verify(dialog)
  tryCompare(dialog,"visible",true)
  const scroll=findChild(dialog,"privacyPolicyScroll"); const body=findChild(dialog,"privacyPolicyBody")
  verify(scroll); verify(body); verify(body.readOnly)
  verify(body.text.indexOf("agplayer@foxmail.com")>=0)
  verify(body.text.indexOf("Cloudflare")>=0)
  tryVerify(function(){return scroll.contentItem.contentHeight>scroll.height})
  wait(300)
  if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput)
   grabImage(host.contentItem).save(visualFixtureOutput + "-policy.png")
  let link=""; body.linkActivated.connect(function(value){link=value})
  let linkX=-1,linkY=-1
  for(let y=0;y<body.height && linkX<0;y+=4) for(let x=0;x<body.width;x+=4) if(body.linkAt(x,y).indexOf("https:")===0){linkX=x;linkY=y;break}
  verify(linkX>=0,"Rendered markdown must expose clickable links")
  scroll.contentItem.contentY=Math.max(0,linkY-60); wait(100)
  verify(body.linkAt(linkX,linkY).indexOf("https:")===0); compare(dialog.openLink("javascript:bad"),false)
  scroll.contentItem.contentY=scroll.contentItem.contentHeight-scroll.height
  verify(scroll.contentItem.contentY>0)
  // Exercise the real click signal without launching an external application.
  body.text='<a href="javascript:blocked">Blocked link</a>'
  scroll.contentItem.contentY=0; wait(100)
  let hit=false
  for(let y=0;y<40 && !hit;y+=2) for(let x=0;x<body.width;x+=2) {
   if(body.linkAt(x,y)==="javascript:blocked") { mouseClick(body,body.leftPadding+25,body.topPadding+8); hit=true; break }
  }
  verify(hit); compare(link,"javascript:blocked")
  const close=dialog.standardButton(Dialog.Close); mouseClick(close,close.width/2,close.height/2)
  tryCompare(dialog,"visible",false)
  mouseClick(button,button.width/2,button.height/2); tryCompare(dialog,"visible",true)
  compare(scroll.contentItem.contentY,0)
  keyClick(Qt.Key_Escape); tryCompare(dialog,"visible",false)
  host.close()
 }
}
