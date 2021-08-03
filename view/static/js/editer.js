$.noConflict(); 

(function($){

/****test button ****/
$('.button').click(function(){
    perference.valueGetAync("helo");
    less.modifyVars({
    });
});


CodeMirror.commands.save = function(){ alert("Saving"); };
function buildCodeEditer(TextAreaElement)
{
  return CodeMirror.fromTextArea(TextAreaElement, {
    keyMap: "vim",
    inputStyle: "contenteditable",
    matchBrackets: true,
    showCursorWhenSelecting: true,
    lineNumbers: true,
    highlightSelectionMatches: {showToken: /\w/, annotateScrollbar: true}, 
    viewportMargin: 100,
    theme: "darcula",
    showCursorWhenSelecting: true,
  });
}
var editers = [];
editers.push({ id: 'code', editer: buildCodeEditer(document.getElementById('code')) });
$('.CodeMirror-vscrollbar').addClass('editer-body-view-scroll-base');
$('.CodeMirror-hscrollbar').addClass('editer-body-view-scroll-base');

/**
editor.getValue();
  this.codemirror.setValue('')
        setTimeout(() => {
          this.codemirror.refresh()
        }, 1)


        editor.getOption("theme")
editor.getOption("theme","seti")

extraKeys:{
   "Ctrl-Space":"autocomplete",
   "Ctrl-F7":function () {console.log("自定义事件");},
}
 */





/****editer-body-resource-bar-slider-Action****/
/****editer-body-xslider****/
(function($){

  var isClick = true;
  var mouseStatus = false;
  var hideWhenMouseDown=false;
  var editerResource= $('.editer-body-resource');
  var editerPreview = $('.editer-body-preview');
  var editerEdition = $('.editer-body-edition');
  var resourceList  = $('#resource-list');
  var resourceElems = $('#resource-elems');
  var visualEdition = $('#visual-edition');
  var codingEdition = $('#coding-edition');
  var codingEditionBox=$('.CodeMirror');
  var codingEditionBar=$('.code-edition-status-bar');
  var configPreview = $('#config-preview');
  var otherPreview  = $('#other-preview');

  var resourceElemsH = resourceElems.height();
  var visualEditionH = visualEdition.height();
  var configPreviewH = configPreview.height();

  var whichslider='';
  var c=0;
  var initX=0;
  var initY=0;

  $('.editer').on('mouseup dragend', function(event){
      mouseStatus=false;
      if (Math.abs(initX-event.pageX)+Math.abs(initY-event.pageY)>=3){
        isClick = false;
      }
  });
  $('.editer-body-xslider, #resource-list-bar, #coding-edition-bar, #other-preview-bar').mousedown(function(event){
      mouseStatus = true;
      isClick = true;
      initX = event.pageX;
      initY = event.pageY;

      var targetID = $(event.target).attr('id');
      if (targetID == "left-slider-body-edition") {
          c = event.pageX - editerResource.width();
          whichslider='left-slider-body-edition';
      }else if (targetID == "right-slider-body-edition") {
          c = event.pageX + editerPreview.width();
          whichslider='right-slider-body-edition';
      }else if (targetID == "resource-list-bar") {
          c = event.pageY + resourceList.height();
          resourceElemsH = resourceElems.height();
          hideWhenMouseDown = resourceList.is(':hidden');
          whichslider='resource-list-bar';
      }else if (targetID == "coding-edition-bar") {
          c = event.pageY + codingEdition.height();
          visualEditionH = visualEdition.height();
          hideWhenMouseDown = codingEdition.is(':hidden');
          whichslider='coding-edition-bar';
      }else if (targetID == "other-preview-bar") {
          c = event.pageY + otherPreview.height();
          configPreviewH = configPreview.height();
          hideWhenMouseDown = otherPreview.is(':hidden');
          whichslider='other-preview-bar';
      }else{
          whichslider='';
          mouseStatus=false;
      }
      event.stopPropagation();
      event.preventDefault();
  });
  $('.editer-body').mousemove(function(event) {
      if (mouseStatus){
        if (whichslider == "left-slider-body-edition" && initX<event.pageX && editerEdition.width()<165) {
            mouseStatus = false;
            return;
        }
        if (whichslider == "right-slider-body-edition" && initX>event.pageX && editerEdition.width()<165) {
            mouseStatus = false;
            return;
        }

        if (whichslider == "left-slider-body-edition") {
          editerResource.width(event.pageX-c + 'px');
        }else if (whichslider == "right-slider-body-edition") {
            editerPreview.width(c - event.pageX + 'px');
        }else if (whichslider == "resource-list-bar" && Math.abs(initY-event.pageY)>3) {
            var h = c - event.pageY;
            if (resourceList.is(':hidden') && initY > event.pageY){
                resourceList.height(0)
                            .css('display', 'block');
                c = event.pageY + resourceList.height();
            } else if (resourceElems.height() < editerResource.height()*0.2 && initY > event.pageY) {
                mouseStatus = false;
            } else {
                if (h < 40 && !hideWhenMouseDown) {
                    mouseStatus = false;
                    resourceList.slideUp('fast');
                } else {
                    resourceList.height(h + 'px');
                }
            }
        }else if (whichslider == "coding-edition-bar" && Math.abs(initY-event.pageY)>3) {
            var h = c - event.pageY;
            if (codingEdition.is(':hidden') && initY > event.pageY){
                codingEdition.height(0)
                            .css('display', 'block');
                c = event.pageY + codingEdition.height();
            } else if (visualEdition.height() < editerEdition.height()*0.2 && initY > event.pageY) {
                mouseStatus = false;
            } else {
                if (h < 40 && !hideWhenMouseDown) {
                    mouseStatus = false;
                    codingEdition.slideUp('fast');
                } else {
                    codingEdition.height(h + 'px');
                    codingEditionBox.height(h-codingEditionBar.height() + 'px');
                }
            }
        }else if (whichslider == "other-preview-bar" && Math.abs(initY-event.pageY)>3) {
            var h = c - event.pageY;
            if (otherPreview.is(':hidden') && initY > event.pageY){
                otherPreview.height(0)
                            .css('display', 'block');
                c = event.pageY + otherPreview.height();
            } else if (configPreview.height() < editerPreview.height()*0.2 && initY > event.pageY) {
                mouseStatus = false;
            } else {
                if (h < 40 && !hideWhenMouseDown) {
                    mouseStatus = false;
                    otherPreview.slideUp('fast');
                } else {
                    otherPreview.height(h + 'px');
                }
            }
        }


      }
      event.stopPropagation();
  });
   $('#resource-list-bar, #coding-edition-bar, #other-preview-bar').click(function(event){
       if (isClick) {

           var targetId = $(event.target).attr('id');
           if (targetId == "resource-list-bar"  ||
               targetId == "coding-edition-bar" ||
               targetId == "other-preview-bar" ) {

              var target = $(event.target).next();
              if (target.height()<40){target.height(40);}
              target.slideToggle('fast');
            } 

//           if ($(event.target).is('#resource-list-bar')){
//              var target = $(event.target).next();
//              if (target.height()<40){target.height(40);}
//              target.slideToggle('fast');
//           } else if ($(event.target).is('#coding-edition-bar')){
//              var target = $(event.target).next();
//              if (target.height()<40){target.height(40);}
//              target.slideToggle('fast');
//           } else if ($(event.target).is('#other-preview-bar')){
//              var target = $(event.target).next();
//              if (target.height()<40){target.height(40);}
//              target.slideToggle('fast');
//           }
//
       }
   });


  $(window).resize(function(){
     var height = resourceElems.height();
     var delta  = resourceElemsH/height;
     var halfh  = resourceList.height()/2.2;
     var hide   = resourceList.is(':hidden');
     if ((!hide &&  delta > 2 && height < editerResource.height()*0.21) 
         || (hide &&  halfh > editerResource.height()*0.35)) {
          resourceList.height(halfh);
          if (halfh <= 40) {
              resourceList.slideUp('fast');
          }
     }
     resourceElemsH = height;


     height= visualEdition.height();
     delta = visualEditionH/height;
     halfh = codingEdition.height()/2.2;
     hide  = codingEdition.is(':hidden');
     if ((!hide &&  delta > 2 && height < editerEdition.height()*0.21)
         || (hide &&  halfh > editerEdition.height()*0.35)) {
          codingEdition.height(halfh);
          codingEditionBox.height(halfh-codingEditionBar.height() + 'px');
          if (halfh <= 40) {
              codingEdition.slideUp('fast');
          }
     }
     visualEditionH = height;


     height= configPreview.height();
     delta = configPreviewH/height;
     halfh = otherPreview.height()/2.2;
     hide  = otherPreview.is(':hidden');
     if ((!hide && delta > 2 && height < editerPreview.height()*0.21) 
         || (hide &&  halfh > editerPreview.height()*0.35)) {
          otherPreview.height(halfh);
          if (halfh <= 40) {
              otherPreview.slideUp('fast');
          }
     }
     configPreviewH = height;

  });


var idNum = 2;
$('button').click(function(){
    var ed = '<div class="code-edition-xslider"></div>';
    ed += '<div id="code-edition-';
    ed += idNum;
    ed += '" class="code-edition-sub">';
    ed += '<div id="code-edition-';
    ed += idNum;
    ed += '" class="code-edition-sub">';
    ed += '<div class="code-edition-status-bar"></div> <div class="code-edition-editer-content">';
    ed += '<form><textarea id="code-';
    ed += idNum;
    ed += '" name="code"></textarea></form>';
    ed += '</div> </div>';
    $(ed).insertBefore('#code-edition-xslider-disable');

    var id = 'code-' + idNum;
    editers.push({ id: id, editer: buildCodeEditer(document.getElementById(id)) });
    $('.CodeMirror-vscrollbar').addClass('editer-body-view-scroll-base');
    $('.CodeMirror-hscrollbar').addClass('editer-body-view-scroll-base');
    codingEditionBox=$('.CodeMirror');
    codingEditionBar=$('.code-edition-status-bar');
    codingEditionBox.height(codingEdition.height()-codingEditionBar.height() + 'px');
    idNum += 1;
});



})($);



})(jQuery);


